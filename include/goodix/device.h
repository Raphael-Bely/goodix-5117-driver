/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_DEVICE_H
#define GOODIX_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include <libusb-1.0/libusb.h>

#define GOODIX_VID 0x27c6
#define GOODIX_PID 0x5117

/* Bulk endpoints live on interface 1; interface 0 only exposes an unused
 * interrupt endpoint. */
#define GOODIX_INTERFACE 1
#define GOODIX_EP_OUT    0x01
#define GOODIX_EP_IN     0x81

#define GOODIX_MAX_PACKET 64
#define GOODIX_TIMEOUT_MS 1000

/* Short timeout used when flushing leftovers, not when expecting an answer. */
#define GOODIX_DRAIN_TIMEOUT_MS 50

/* Kept below libusb's own error range so both can share a return value. */
enum {
    GOODIX_OK            =    0,
    GOODIX_ERR_IO        = -100,
    GOODIX_ERR_BUFFER    = -101,
    GOODIX_ERR_CHECKSUM  = -102,
    GOODIX_ERR_SHORT     = -103,
    GOODIX_ERR_PROTOCOL  = -104,
    GOODIX_ERR_TLS       = -105,
};

typedef struct {
    libusb_context       *ctx;
    libusb_device_handle *handle;
    int                   claimed;
    /* Raise this around commands that block on hardware, such as waiting for
     * a finger. Set to GOODIX_TIMEOUT_MS by goodix_open(). */
    unsigned              timeout_ms;
} goodix_device;

int  goodix_open(goodix_device *dev);
void goodix_close(goodix_device *dev);

int goodix_write(goodix_device *dev, const uint8_t *data, size_t len);
int goodix_read(goodix_device *dev, uint8_t *buf, size_t len, size_t *received);

/* Discards anything still queued on the IN endpoint. Returns the number of
 * bytes thrown away, or a negative error code. */
int goodix_drain(goodix_device *dev);

/* USB port reset: the software equivalent of unplugging the device. Clears a
 * firmware state machine left half-way through an exchange. The handle may
 * become stale afterwards, so reopen before using the sensor again. */
int goodix_reset(goodix_device *dev);

const char *goodix_strerror(int code);

#endif /* GOODIX_DEVICE_H */
