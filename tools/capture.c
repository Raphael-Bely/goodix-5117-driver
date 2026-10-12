/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Captures one background frame and one finger frame, then writes the
 * difference between them. */

#include <stdio.h>

#include "goodix/capture.h"
#include "goodix/image.h"

static void save_raw(const char *path, const uint8_t *raw)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return;

    fwrite(raw, 1, GOODIX_IMAGE_RAW_SIZE, file);
    fclose(file);
}

int main(void)
{
    goodix_device  dev;
    goodix_session session;
    uint8_t        background_raw[GOODIX_IMAGE_RAW_SIZE];
    uint8_t        finger_raw[GOODIX_IMAGE_RAW_SIZE];
    int            rc;
    int            status = 1;

    setvbuf(stdout, NULL, _IONBF, 0);

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    printf("opening the encrypted channel\n");
    rc = goodix_session_open(&session, &dev, NULL, 0);
    if (rc < 0) {
        fprintf(stderr, "session: %s (%s)\n",
                goodix_strerror(rc), goodix_tls_last_error());
        goto out;
    }

    rc = goodix_capture_prepare(&session);
    if (rc < 0)
        goto fail;

    printf("capturing the background, keep your finger off\n");
    rc = goodix_capture_frame(&session, background_raw);
    if (rc < 0)
        goto fail;
    save_raw("frame-background.bin", background_raw);

    printf("\n  >>> PUT YOUR FINGER ON THE SENSOR AND HOLD IT <<<\n\n");
    rc = goodix_capture_wait_finger(&session, GOODIX_FINGER_TIMEOUT_MS);
    if (rc < 0) {
        fprintf(stderr, "no finger detected: %s\n", goodix_strerror(rc));
        goto out;
    }

    printf("capturing the finger\n");
    rc = goodix_capture_frame(&session, finger_raw);
    if (rc < 0)
        goto fail;
    save_raw("frame-finger.bin", finger_raw);

    printf("\n  >>> LIFT YOUR FINGER <<<\n");
    goodix_capture_wait_lift(&session, GOODIX_FINGER_TIMEOUT_MS);

    printf("\nwrote frame-background.bin and frame-finger.bin\n");
    printf("run build/process to turn them into PGM files\n");
    status = 0;
    goto out;

fail:
    fprintf(stderr, "capture: %s\n", goodix_strerror(rc));

out:
    goodix_session_close(&session);
    goodix_close(&dev);
    return status;
}
