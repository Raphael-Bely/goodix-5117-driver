/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Recovers a sensor left half-way through an exchange: flushes anything still
 * queued, then performs a USB port reset. */

#include <stdio.h>

#include "goodix/device.h"

int main(void)
{
    goodix_device dev;
    int           rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    rc = goodix_drain(&dev);
    if (rc < 0)
        printf("drain: %s\n", goodix_strerror(rc));
    else
        printf("drain: discarded %d byte(s)\n", rc);

    rc = goodix_reset(&dev);
    printf("usb reset: %s\n", rc < 0 ? goodix_strerror(rc) : "ok");

    goodix_close(&dev);
    return rc < 0 ? 1 : 0;
}
