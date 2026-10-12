/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Runs the initialisation sequence and prints what it learned. */

#include <stdio.h>

#include "goodix/init.h"

static void on_step(int step, int total, const char *label, void *user)
{
    (void)user;
    printf("%2d/%d  %s\n", step, total, label);
}

static void print_report(const goodix_init_report *report)
{
    printf("\nfirmware:    %s\n", report->firmware);
    printf("calibration: %04x %04x %04x %04x\n",
           report->calibration[0], report->calibration[1],
           report->calibration[2], report->calibration[3]);

    printf("sensor key:  ");
    for (int i = 0; i < GOODIX_PSK_SIZE; i++)
        printf("%02x", report->psk[i]);
    printf("\n             %s\n",
           report->psk_is_factory
               ? "factory value"
               : "NOT the factory value, see SPEC.md section 10");
}

int main(void)
{
    goodix_device      dev;
    goodix_init_report report;
    int                rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    rc = goodix_initialise(&dev, on_step, NULL, &report);
    if (rc < 0) {
        fprintf(stderr, "initialise: %s\n", goodix_strerror(rc));
        goodix_close(&dev);
        return 1;
    }

    print_report(&report);

    goodix_close(&dev);
    return 0;
}
