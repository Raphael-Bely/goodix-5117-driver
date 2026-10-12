/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_IMAGE_H
#define GOODIX_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "goodix/protocol.h"

/* Unpacks a raw frame into GOODIX_IMAGE_PIXELS samples of 12 bits. The header
 * and trailer are skipped; `raw` must hold GOODIX_IMAGE_RAW_SIZE bytes and
 * `pixels` room for GOODIX_IMAGE_PIXELS entries. */
void goodix_image_decode(const uint8_t *raw, uint16_t *pixels);

/* Keeps the 64 columns that carry data, dropping the 24 dead ones. `out`
 * needs room for GOODIX_IMAGE_USEFUL_PIXELS entries. */
void goodix_image_crop(const uint16_t *pixels, uint16_t *out);

/* Rescales samples onto 0..255 from their own minimum and maximum. */
void goodix_image_normalise(const uint16_t *pixels, uint8_t *out, size_t count);

/* Per-pixel difference between a frame and the background taken just before
 * it. The result is signed: ridges may read above or below the background. */
void goodix_image_subtract(const uint16_t *frame, const uint16_t *background,
                           int16_t *out, size_t count);

/* Rescales signed samples onto 0..255 from their own minimum and maximum. */
void goodix_image_normalise_signed(const int16_t *pixels, uint8_t *out,
                                   size_t count);

/* Default neighbourhood for goodix_image_enhance, chosen to span a few ridges
 * at this sensor's resolution. */
#define GOODIX_ENHANCE_WINDOW 15

/* Local mean and variance normalisation. Each pixel is recentred on its own
 * neighbourhood, which flattens slow illumination changes such as the darker
 * edges, and equalises contrast so that a dry and a moist finger compare on
 * the same footing. */
void goodix_image_enhance(const uint8_t *in, uint8_t *out,
                          int width, int height, int window);

/* Binary PGM (P5), written by hand. The 16-bit variant stores samples most
 * significant byte first, as the format requires — the opposite of the
 * Goodix protocol. */
int goodix_image_write_pgm16(const char *path, const uint16_t *pixels,
                             int width, int height, uint16_t maxval);
int goodix_image_write_pgm8(const char *path, const uint8_t *pixels,
                            int width, int height);

#endif /* GOODIX_IMAGE_H */
