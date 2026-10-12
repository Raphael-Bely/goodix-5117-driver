/*
 * Goodix 27c6:5117 driver
 * Copyright (C) 2026 Raphaël Bély
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * The sensor talks over a TLS channel and answers finger detection only when
 * a finger actually lands, so every exchange blocks. libfprint's entry points
 * must return at once, so the blocking half runs on a worker thread and hands
 * results back through the device's main context.
 *
 * The rule the whole file rests on: fpi_image_device_* is called from the
 * main thread and nowhere else.
 */

#define FP_COMPONENT "goodix5117"

#include <string.h>

#include "drivers_api.h"

#include "goodix/capture.h"
#include "goodix/image.h"
#include "goodix/match.h"
#include "goodix/session.h"

typedef enum {
  WORK_NONE = 0,
  WORK_OPEN,
  WORK_ACTIVATE,
  WORK_AWAIT_FINGER_ON,
  WORK_CAPTURE,
  WORK_AWAIT_FINGER_OFF,
  WORK_DEACTIVATE,
  WORK_CLOSE,
} WorkKind;

struct _FpiDeviceGoodix5117
{
  FpImageDevice  parent;

  GThread       *worker;
  GMutex         lock;          /* guards `pending` and `quit` */
  GCond          wake;
  WorkKind       pending;
  gboolean       quit;

  GMainContext  *context;       /* where results must be delivered */

  goodix_device  dev;
  goodix_session session;

  /* Captured with the finger off just before each touch, and subtracted
   * from the finger frame. */
  uint8_t        background[GOODIX_IMAGE_RAW_SIZE];
  uint8_t        frame[GOODIX_IMAGE_RAW_SIZE];
};

G_DECLARE_FINAL_TYPE (FpiDeviceGoodix5117, fpi_device_goodix5117, FPI,
                      DEVICE_GOODIX5117, FpImageDevice);
G_DEFINE_TYPE (FpiDeviceGoodix5117, fpi_device_goodix5117,
               FP_TYPE_IMAGE_DEVICE);

static const FpIdEntry id_table[] = {
  { .vid = 0x27c6, .pid = 0x5117, },
  { .vid = 0, .pid = 0, .driver_data = 0 },
};

/* ---- main thread -> worker ---------------------------------------------- */

static void
post_work (FpiDeviceGoodix5117 *self, WorkKind kind)
{
  g_mutex_lock (&self->lock);

  if (self->pending != WORK_NONE)
    fp_warn ("work %d dropped, %d still pending", kind, self->pending);

  self->pending = kind;
  g_cond_signal (&self->wake);
  g_mutex_unlock (&self->lock);
}

/* ---- worker -> main thread ---------------------------------------------- */

typedef struct {
  FpiDeviceGoodix5117 *self;
  WorkKind             kind;
  int                  rc;
} Result;

/* Turns the two captures into the image libfprint expects. */
static FpImage *
build_image (FpiDeviceGoodix5117 *self)
{
  uint16_t background[GOODIX_IMAGE_PIXELS];
  uint16_t finger[GOODIX_IMAGE_PIXELS];
  uint16_t background_useful[GOODIX_IMAGE_USEFUL_PIXELS];
  uint16_t finger_useful[GOODIX_IMAGE_USEFUL_PIXELS];
  int16_t  difference[GOODIX_IMAGE_USEFUL_PIXELS];
  uint8_t  stretched[GOODIX_IMAGE_USEFUL_PIXELS];
  FpImage *image;

  goodix_image_decode (self->background, background);
  goodix_image_decode (self->frame, finger);
  goodix_image_crop (background, background_useful);
  goodix_image_crop (finger, finger_useful);

  goodix_image_subtract (finger_useful, background_useful, difference,
                         GOODIX_IMAGE_USEFUL_PIXELS);
  goodix_image_normalise_signed (difference, stretched,
                                 GOODIX_IMAGE_USEFUL_PIXELS);

  image = fp_image_new (GOODIX_IMAGE_USEFUL_WIDTH, GOODIX_IMAGE_HEIGHT);

  /* Local equalisation writes straight into the image buffer. */
  goodix_image_enhance (stretched, image->data, GOODIX_IMAGE_USEFUL_WIDTH,
                        GOODIX_IMAGE_HEIGHT, GOODIX_ENHANCE_WINDOW);

  /* The sensor only ever sees a fraction of a fingertip. */
  image->flags |= FPI_IMAGE_PARTIAL;

  return image;
}

/* build_image already leaves the finished template in image->data, so the
 * stored form is simply those bytes. */
static GVariant *
dev_serialize_image (FpImageDevice *dev, FpImage *image)
{
  (void) dev;

  if (image->width != GOODIX_IMAGE_USEFUL_WIDTH ||
      image->height != GOODIX_IMAGE_HEIGHT)
    return NULL;

  return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, image->data,
                                    GOODIX_IMAGE_USEFUL_PIXELS, 1);
}

/* The probe is one template, `enrolled` the several collected so far. A touch
 * rarely lands on the same spot twice, so any one of them matching is enough.
 *
 * Enrolment asks a stricter question than verification: a capture that only
 * just clears the match threshold covers a different patch of skin, and
 * keeping it would drag every later score down. */
static gboolean
dev_compare_data (FpImageDevice *dev, GVariant *probe, GVariant *enrolled,
                  gboolean enrolling)
{
  const double threshold = enrolling ? GOODIX_ENROL_MIN_AGREEMENT
                                     : GOODIX_MATCH_THRESHOLD;
  const goodix_template *candidate;
  GVariantIter           iter;
  GVariant              *stored;
  gsize                  size = 0;
  gboolean               matched = FALSE;

  (void) dev;

  candidate = g_variant_get_fixed_array (probe, &size, 1);
  if (candidate == NULL || size != sizeof (goodix_template))
    return FALSE;

  if (!g_variant_is_of_type (enrolled, G_VARIANT_TYPE ("aay")))
    return FALSE;

  g_variant_iter_init (&iter, enrolled);
  while (!matched && (stored = g_variant_iter_next_value (&iter)))
    {
      const goodix_template *reference;
      gsize                  reference_size = 0;

      reference = g_variant_get_fixed_array (stored, &reference_size, 1);

      if (reference && reference_size == sizeof (goodix_template))
        {
          double score = goodix_match_score (candidate, reference);

          fp_dbg ("correlation %.3f (%s threshold %.2f)", score,
                  enrolling ? "enrol" : "match", threshold);
          matched = score >= threshold;
        }

      g_variant_unref (stored);
    }

  return matched;
}

static void
stop_worker (FpiDeviceGoodix5117 *self)
{
  if (self->worker == NULL)
    return;

  g_mutex_lock (&self->lock);
  self->quit = TRUE;
  g_cond_signal (&self->wake);
  g_mutex_unlock (&self->lock);

  g_thread_join (self->worker);
  self->worker = NULL;
}

static gboolean
deliver (gpointer data)
{
  Result              *result = data;
  FpiDeviceGoodix5117 *self   = result->self;
  FpImageDevice       *dev    = FP_IMAGE_DEVICE (self);
  GError              *error  = NULL;

  if (result->rc < 0)
    error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "%s",
                                      goodix_strerror (result->rc));

  switch (result->kind)
    {
    case WORK_OPEN:
      fpi_image_device_open_complete (dev, error);
      break;

    case WORK_ACTIVATE:
      fpi_image_device_activate_complete (dev, error);
      break;

    case WORK_AWAIT_FINGER_ON:
      if (error)
        fpi_image_device_session_error (dev, error);
      else
        fpi_image_device_report_finger_status (dev, TRUE);
      break;

    case WORK_CAPTURE:
      if (error)
        fpi_image_device_session_error (dev, error);
      else
        fpi_image_device_image_captured (dev, build_image (self));
      break;

    case WORK_AWAIT_FINGER_OFF:
      if (error)
        fpi_image_device_session_error (dev, error);
      else
        fpi_image_device_report_finger_status (dev, FALSE);
      break;

    case WORK_DEACTIVATE:
      fpi_image_device_deactivate_complete (dev, error);
      break;

    case WORK_CLOSE:
      /* The worker has nothing left to do; retire it before answering. */
      stop_worker (self);
      fpi_image_device_close_complete (dev, error);
      break;

    case WORK_NONE:
    default:
      g_clear_error (&error);
      break;
    }

  g_object_unref (self);
  g_free (result);

  return G_SOURCE_REMOVE;
}

static void
post_result (FpiDeviceGoodix5117 *self, WorkKind kind, int rc)
{
  Result *result = g_new0 (Result, 1);

  /* The device could otherwise be finalised before this lands. */
  result->self = g_object_ref (self);
  result->kind = kind;
  result->rc   = rc;

  g_main_context_invoke (self->context, deliver, result);
}

/* ---- worker thread ------------------------------------------------------ */

static int
run_work (FpiDeviceGoodix5117 *self, WorkKind kind)
{
  int rc;

  switch (kind)
    {
    case WORK_OPEN:
      rc = goodix_open (&self->dev);
      if (rc < 0)
        return rc;

      rc = goodix_session_open (&self->session, &self->dev, NULL, 0);
      if (rc < 0)
        return rc;

      return goodix_capture_prepare (&self->session);

    case WORK_ACTIVATE:
      return GOODIX_OK;

    case WORK_AWAIT_FINGER_ON:
      /* A fresh background before every touch. libfprint activates once and
       * then runs several enroll stages, so a background taken at activation
       * would be seconds stale by the last one — and the sensor drifts as it
       * warms up, which the subtraction would then carry into the template. */
      rc = goodix_capture_frame (&self->session, self->background);
      if (rc < 0)
        return rc;

      return goodix_capture_wait_finger (&self->session,
                                         GOODIX_FINGER_TIMEOUT_MS);

    case WORK_CAPTURE:
      return goodix_capture_frame (&self->session, self->frame);

    case WORK_AWAIT_FINGER_OFF:
      return goodix_capture_wait_lift (&self->session,
                                       GOODIX_FINGER_TIMEOUT_MS);

    case WORK_DEACTIVATE:
      return GOODIX_OK;

    case WORK_CLOSE:
      goodix_session_close (&self->session);
      goodix_close (&self->dev);
      return GOODIX_OK;

    case WORK_NONE:
    default:
      return GOODIX_OK;
    }
}

static gpointer
worker_main (gpointer data)
{
  FpiDeviceGoodix5117 *self = data;

  for (;;)
    {
      WorkKind kind;
      int      rc;

      g_mutex_lock (&self->lock);
      /* g_cond_wait releases the mutex while asleep and takes it back on
       * wake-up; without that the main thread could never post anything. */
      while (self->pending == WORK_NONE && !self->quit)
        g_cond_wait (&self->wake, &self->lock);

      if (self->quit)
        {
          g_mutex_unlock (&self->lock);
          break;
        }

      kind = self->pending;
      self->pending = WORK_NONE;
      g_mutex_unlock (&self->lock);

      /* Blocking is allowed here, and only here. */
      rc = run_work (self, kind);

      post_result (self, kind, rc);
    }

  return NULL;
}

/* ---- libfprint entry points, all on the main thread --------------------- */

static void
dev_open (FpImageDevice *dev)
{
  FpiDeviceGoodix5117 *self = FPI_DEVICE_GOODIX5117 (dev);

  /* Results must land in the context libfprint is running on, which is not
   * necessarily the global default one. */
  self->context = g_main_context_ref_thread_default ();

  g_mutex_init (&self->lock);
  g_cond_init (&self->wake);
  self->pending = WORK_NONE;
  self->quit    = FALSE;

  self->worker = g_thread_new ("goodix5117", worker_main, self);

  post_work (self, WORK_OPEN);
}

static void
dev_close (FpImageDevice *dev)
{
  post_work (FPI_DEVICE_GOODIX5117 (dev), WORK_CLOSE);
}

static void
dev_activate (FpImageDevice *dev)
{
  post_work (FPI_DEVICE_GOODIX5117 (dev), WORK_ACTIVATE);
}

static void
dev_deactivate (FpImageDevice *dev)
{
  post_work (FPI_DEVICE_GOODIX5117 (dev), WORK_DEACTIVATE);
}

static void
dev_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  FpiDeviceGoodix5117 *self = FPI_DEVICE_GOODIX5117 (dev);

  switch (state)
    {
    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:
      post_work (self, WORK_AWAIT_FINGER_ON);
      break;

    case FPI_IMAGE_DEVICE_STATE_CAPTURE:
      post_work (self, WORK_CAPTURE);
      break;

    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF:
      post_work (self, WORK_AWAIT_FINGER_OFF);
      break;

    case FPI_IMAGE_DEVICE_STATE_INACTIVE:
    case FPI_IMAGE_DEVICE_STATE_ACTIVATING:
    case FPI_IMAGE_DEVICE_STATE_DEACTIVATING:
    case FPI_IMAGE_DEVICE_STATE_IDLE:
    default:
      break;
    }
}

static void
fpi_device_goodix5117_finalize (GObject *object)
{
  FpiDeviceGoodix5117 *self = FPI_DEVICE_GOODIX5117 (object);

  stop_worker (self);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->wake);
  g_clear_pointer (&self->context, g_main_context_unref);

  G_OBJECT_CLASS (fpi_device_goodix5117_parent_class)->finalize (object);
}

static void
fpi_device_goodix5117_init (FpiDeviceGoodix5117 *self)
{
}

static void
fpi_device_goodix5117_class_init (FpiDeviceGoodix5117Class *klass)
{
  GObjectClass       *object_class = G_OBJECT_CLASS (klass);
  FpDeviceClass      *dev_class    = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class    = FP_IMAGE_DEVICE_CLASS (klass);

  object_class->finalize = fpi_device_goodix5117_finalize;

  dev_class->id        = FP_COMPONENT;
  dev_class->full_name = "Goodix 5117 Fingerprint Sensor";
  dev_class->type      = FP_DEVICE_TYPE_USB;
  dev_class->id_table  = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  img_class->img_open     = dev_open;
  img_class->img_close    = dev_close;
  img_class->activate     = dev_activate;
  img_class->deactivate   = dev_deactivate;
  img_class->change_state = dev_change_state;

  /* Frames are 64 x 80; NBIS finds at most one minutia on them, so the
   * driver compares by correlation instead. See SPEC.md section 11. */
  img_class->serialize_image = dev_serialize_image;
  img_class->compare_data    = dev_compare_data;

  img_class->img_width  = GOODIX_IMAGE_USEFUL_WIDTH;
  img_class->img_height = GOODIX_IMAGE_HEIGHT;

  /* The frame is far too small for the default minutiae matcher to be
   * reliable; see SPEC.md section 11. */
  img_class->bz3_threshold = 24;
}
