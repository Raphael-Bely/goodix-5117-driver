/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Lists what libfprint sees, using the locally built library.
 *
 * Not part of the default build: it links against libfprint rather than our
 * own code. Run `. ./libfprint-env.sh` first, then `make fplist`. */

#include <stdio.h>
#include <fprint.h>

int main(void)
{
    g_autoptr(FpContext) ctx = fp_context_new();
    GPtrArray *devices = fp_context_get_devices(ctx);

    if (devices == NULL || devices->len == 0) {
        printf("no device found\n");
        return 1;
    }

    for (guint i = 0; i < devices->len; i++) {
        FpDevice *dev = g_ptr_array_index(devices, i);

        printf("device %u\n", i);
        printf("  driver    : %s\n", fp_device_get_driver(dev));
        printf("  name      : %s\n", fp_device_get_name(dev));
        printf("  scan type : %s\n",
               fp_device_get_scan_type(dev) == FP_SCAN_TYPE_PRESS
                   ? "press" : "swipe");
        printf("  device id : %s\n", fp_device_get_device_id(dev));
    }

    return 0;
}
