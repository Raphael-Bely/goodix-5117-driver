/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <string.h>

#include "goodix/device.h"

int goodix_open(goodix_device *dev)
{
    libusb_device **list  = NULL;
    libusb_device  *found = NULL;
    ssize_t count;
    int rc;

    memset(dev, 0, sizeof(*dev));

    rc = libusb_init(&dev->ctx);
    if (rc < 0)
        return rc;

    count = libusb_get_device_list(dev->ctx, &list);
    if (count < 0) {
        rc = (int)count;
        goto fail;
    }

    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(list[i], &desc) == 0 &&
            desc.idVendor == GOODIX_VID && desc.idProduct == GOODIX_PID) {
            found = list[i];
            break;
        }
    }

    if (found == NULL) {
        rc = LIBUSB_ERROR_NO_DEVICE;
        goto fail;
    }

    rc = libusb_open(found, &dev->handle);
    if (rc < 0)
        goto fail;

    rc = libusb_claim_interface(dev->handle, GOODIX_INTERFACE);
    if (rc < 0)
        goto fail;

    dev->claimed    = 1;
    dev->timeout_ms = GOODIX_TIMEOUT_MS;
    libusb_free_device_list(list, 1);
    return GOODIX_OK;

fail:
    if (list)
        libusb_free_device_list(list, 1);
    goodix_close(dev);
    return rc;
}

void goodix_close(goodix_device *dev)
{
    if (dev->claimed) {
        libusb_release_interface(dev->handle, GOODIX_INTERFACE);
        dev->claimed = 0;
    }
    if (dev->handle) {
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }
}

int goodix_write(goodix_device *dev, const uint8_t *data, size_t len)
{
    int transferred = 0;
    int rc;

    /* libusb takes a non-const buffer for both directions. */
    rc = libusb_bulk_transfer(dev->handle, GOODIX_EP_OUT, (uint8_t *)data,
                              (int)len, &transferred, dev->timeout_ms);
    if (rc < 0)
        return rc;

    return (size_t)transferred == len ? GOODIX_OK : GOODIX_ERR_IO;
}

int goodix_read(goodix_device *dev, uint8_t *buf, size_t len, size_t *received)
{
    int transferred = 0;
    int rc;

    rc = libusb_bulk_transfer(dev->handle, GOODIX_EP_IN, buf, (int)len,
                              &transferred, dev->timeout_ms);
    if (rc < 0)
        return rc;

    *received = (size_t)transferred;
    return GOODIX_OK;
}

int goodix_drain(goodix_device *dev)
{
    uint8_t buf[GOODIX_MAX_PACKET];
    int     discarded = 0;
    int     transferred;
    int     rc;

    for (;;) {
        rc = libusb_bulk_transfer(dev->handle, GOODIX_EP_IN, buf, sizeof(buf),
                                  &transferred, GOODIX_DRAIN_TIMEOUT_MS);
        if (rc == LIBUSB_ERROR_TIMEOUT)
            return discarded;
        if (rc < 0)
            return rc;
        if (transferred == 0)
            return discarded;

        discarded += transferred;
    }
}

int goodix_reset(goodix_device *dev)
{
    int rc = libusb_reset_device(dev->handle);

    return rc < 0 ? rc : GOODIX_OK;
}

const char *goodix_strerror(int code)
{
    switch (code) {
    case GOODIX_OK:           return "success";
    case GOODIX_ERR_IO:       return "short or empty transfer";
    case GOODIX_ERR_BUFFER:   return "buffer too small";
    case GOODIX_ERR_CHECKSUM: return "checksum mismatch";
    case GOODIX_ERR_SHORT:    return "truncated message";
    case GOODIX_ERR_PROTOCOL: return "unexpected protocol reply";
    case GOODIX_ERR_TLS:      return "TLS failure";
    default:                  return libusb_error_name(code);
    }
}
