/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_CAPTURE_H
#define GOODIX_CAPTURE_H

#include <stdint.h>

#include "goodix/session.h"

/* Finger detection is a hardware feature: the sensor answers only once it
 * feels something, so these calls block and need a human-sized timeout. */
#define GOODIX_FINGER_TIMEOUT_MS 20000

/* Puts the sensor in capture mode: mode switches, register read and this
 * chip's own calibration values. Call once per session. */
int goodix_capture_prepare(goodix_session *session);

/* Grabs one frame right away, finger or not. */
int goodix_capture_frame(goodix_session *session, uint8_t *raw);

/* Arms finger detection and blocks until a finger lands. */
int goodix_capture_wait_finger(goodix_session *session, unsigned timeout_ms);

/* Blocks until the finger is lifted, then leaves the sensor where the
 * reference driver leaves it — without which it stops answering. */
int goodix_capture_wait_lift(goodix_session *session, unsigned timeout_ms);

#endif /* GOODIX_CAPTURE_H */
