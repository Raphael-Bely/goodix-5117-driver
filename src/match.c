/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "goodix/match.h"

/* Marks the file and guards against reading one written by another build. */
#define ENROLMENT_MAGIC   0x58444789u
#define ENROLMENT_VERSION 1u

void goodix_match_prepare(const uint8_t *background_raw,
                          const uint8_t *finger_raw, goodix_template *out)
{
    uint16_t background[GOODIX_IMAGE_PIXELS];
    uint16_t finger[GOODIX_IMAGE_PIXELS];
    uint16_t background_useful[GOODIX_IMAGE_USEFUL_PIXELS];
    uint16_t finger_useful[GOODIX_IMAGE_USEFUL_PIXELS];
    int16_t  difference[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t  stretched[GOODIX_IMAGE_USEFUL_PIXELS];

    goodix_image_decode(background_raw, background);
    goodix_image_decode(finger_raw, finger);
    goodix_image_crop(background, background_useful);
    goodix_image_crop(finger, finger_useful);

    goodix_image_subtract(finger_useful, background_useful, difference,
                          GOODIX_IMAGE_USEFUL_PIXELS);
    goodix_image_normalise_signed(difference, stretched,
                                  GOODIX_IMAGE_USEFUL_PIXELS);
    goodix_image_enhance(stretched, out->pixels, GOODIX_IMAGE_USEFUL_WIDTH,
                         GOODIX_IMAGE_HEIGHT, GOODIX_ENHANCE_WINDOW);
}

/* Correlation of `a` against `b` slid by (dx, dy), measured on the part where
 * the two actually overlap. Returns -2.0 when the overlap is too small or
 * either side is flat, which cannot happen for a real correlation. */
static double correlate(const uint8_t *a, const uint8_t *b, int dx, int dy)
{
    const int width  = GOODIX_IMAGE_USEFUL_WIDTH;
    const int height = GOODIX_IMAGE_HEIGHT;

    long sum_a = 0, sum_b = 0;
    long sum_aa = 0, sum_bb = 0, sum_ab = 0;
    int  count = 0;

    double mean_a, mean_b, covariance, variance_a, variance_b;

    for (int y = 0; y < height; y++) {
        int by = y - dy;

        if (by < 0 || by >= height)
            continue;

        for (int x = 0; x < width; x++) {
            int bx = x - dx;
            int va, vb;

            if (bx < 0 || bx >= width)
                continue;

            va = a[y * width + x];
            vb = b[by * width + bx];

            sum_a  += va;
            sum_b  += vb;
            sum_aa += (long)va * va;
            sum_bb += (long)vb * vb;
            sum_ab += (long)va * vb;
            count++;
        }
    }

    if (count < (int)(GOODIX_IMAGE_USEFUL_PIXELS * GOODIX_MATCH_MIN_OVERLAP))
        return -2.0;

    mean_a = (double)sum_a / count;
    mean_b = (double)sum_b / count;

    covariance = (double)sum_ab / count - mean_a * mean_b;
    variance_a = (double)sum_aa / count - mean_a * mean_a;
    variance_b = (double)sum_bb / count - mean_b * mean_b;

    if (variance_a <= 0.0 || variance_b <= 0.0)
        return -2.0;

    return covariance / sqrt(variance_a * variance_b);
}

double goodix_match_score(const goodix_template *a, const goodix_template *b)
{
    double best = -2.0;

    for (int dy = -GOODIX_MATCH_SEARCH; dy <= GOODIX_MATCH_SEARCH; dy++) {
        for (int dx = -GOODIX_MATCH_SEARCH; dx <= GOODIX_MATCH_SEARCH; dx++) {
            double score = correlate(a->pixels, b->pixels, dx, dy);

            if (score > best)
                best = score;
        }
    }

    return best;
}

int goodix_enrolment_save(const char *path, const goodix_enrolment *enrolment)
{
    uint32_t header[3] = { ENROLMENT_MAGIC, ENROLMENT_VERSION,
                           enrolment->count };
    FILE    *file;
    int      fd;

    if (enrolment->count == 0 || enrolment->count > GOODIX_ENROL_MAX)
        return GOODIX_ERR_BUFFER;

    /* Templates are biometric data. A fingerprint cannot be changed once it
     * leaks, so the file is readable by its owner and no one else. */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return GOODIX_ERR_IO;

    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        return GOODIX_ERR_IO;
    }

    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(enrolment->entries, sizeof(enrolment->entries[0]),
               enrolment->count, file) != enrolment->count) {
        fclose(file);
        return GOODIX_ERR_IO;
    }

    return fclose(file) == 0 ? GOODIX_OK : GOODIX_ERR_IO;
}

int goodix_enrolment_load(const char *path, goodix_enrolment *enrolment)
{
    uint32_t header[3];
    FILE    *file = fopen(path, "rb");

    if (file == NULL)
        return GOODIX_ERR_IO;

    memset(enrolment, 0, sizeof(*enrolment));

    if (fread(header, sizeof(header), 1, file) != 1) {
        fclose(file);
        return GOODIX_ERR_SHORT;
    }

    if (header[0] != ENROLMENT_MAGIC || header[1] != ENROLMENT_VERSION ||
        header[2] == 0 || header[2] > GOODIX_ENROL_MAX) {
        fclose(file);
        return GOODIX_ERR_PROTOCOL;
    }

    enrolment->count = header[2];
    if (fread(enrolment->entries, sizeof(enrolment->entries[0]),
              enrolment->count, file) != enrolment->count) {
        fclose(file);
        return GOODIX_ERR_SHORT;
    }

    fclose(file);
    return GOODIX_OK;
}

double goodix_enrolment_score(const goodix_enrolment *enrolment,
                              const goodix_template *probe)
{
    double best = -2.0;

    for (uint32_t i = 0; i < enrolment->count; i++) {
        double score = goodix_match_score(&enrolment->entries[i], probe);

        if (score > best)
            best = score;
    }

    return best;
}
