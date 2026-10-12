/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_MATCH_H
#define GOODIX_MATCH_H

#include <stdint.h>

#include "goodix/image.h"

/* How far the search slides one image over the other, in pixels. A finger
 * rarely lands more than a couple of millimetres off. */
#define GOODIX_MATCH_SEARCH 12

/* Below this fraction of overlapping pixels a score means nothing. */
#define GOODIX_MATCH_MIN_OVERLAP 0.40

/* Chosen from measured distributions. Offline, impostors peaked at 0.28; in
 * daily use through fprintd one reached 0.394 against a 0.40 bar, which is
 * far too close. Raised to 0.50, which still clears well-placed genuine
 * touches (0.51 and 0.74 measured).
 *
 * The two populations overlap more than a larger sensor's would: a badly
 * placed genuine finger scored 0.337, below a different finger's 0.394. On a
 * 64x80 frame placement weighs more than identity, so treat this as a
 * convenience rather than a security gain.
 *
 * A rejected touch costs a retry; an accepted impostor costs everything. */
#define GOODIX_MATCH_THRESHOLD 0.50

/* A touch that agrees with none of the ones already kept covers a different
 * patch of skin. Keeping it scatters the enrolment and drags later scores
 * down, so the tool asks for it again. */
#define GOODIX_ENROL_MIN_AGREEMENT 0.55

/* More templates cover more of the fingertip, which is what makes a sensor
 * this small usable at all. */
#define GOODIX_ENROL_MAX 16

/* A capture reduced to what comparison needs. */
typedef struct {
    uint8_t pixels[GOODIX_IMAGE_USEFUL_PIXELS];
} goodix_template;

/* Runs the whole pipeline on a frame pair: decode, crop, subtract the
 * background, stretch, then equalise contrast locally. */
void goodix_match_prepare(const uint8_t *background_raw,
                          const uint8_t *finger_raw, goodix_template *out);

/* Best normalised correlation over the search window. 1.0 means identical up
 * to brightness and contrast, 0.0 unrelated, negative inverted. */
double goodix_match_score(const goodix_template *a, const goodix_template *b);

/* A set of templates standing for one finger. */
typedef struct {
    uint32_t        count;
    goodix_template entries[GOODIX_ENROL_MAX];
} goodix_enrolment;

int goodix_enrolment_save(const char *path, const goodix_enrolment *enrolment);
int goodix_enrolment_load(const char *path, goodix_enrolment *enrolment);

/* Best score against any stored template. */
double goodix_enrolment_score(const goodix_enrolment *enrolment,
                              const goodix_template *probe);

#endif /* GOODIX_MATCH_H */
