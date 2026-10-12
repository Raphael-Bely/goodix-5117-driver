/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_INIT_H
#define GOODIX_INIT_H

#include <stdint.h>

#include "goodix/protocol.h"

#define GOODIX_INIT_STEPS 10

/* Called before each step when passed to goodix_initialise(); may be NULL. */
typedef void (*goodix_progress_fn)(int step, int total, const char *label,
                                   void *user);

/* Facts gathered while initialising. */
typedef struct {
    char     firmware[64];
    uint8_t  psk[GOODIX_PSK_SIZE];
    int      psk_is_factory;
    uint16_t calibration[4];
} goodix_init_report;

/* Runs the ten-step sequence from SPEC.md section 5. A sensor key that differs
 * from the factory one is reported, never rewritten. `report` may be NULL. */
int goodix_initialise(goodix_device *dev, goodix_progress_fn progress,
                      void *user, goodix_init_report *report);

#endif /* GOODIX_INIT_H */
