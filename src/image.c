/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "goodix/image.h"

void goodix_image_decode(const uint8_t *raw, uint16_t *pixels)
{
    const uint8_t *chunk = raw + GOODIX_IMAGE_HEAD;
    uint16_t      *out   = pixels;

    /* Six bytes carry four samples, interleaved the way the sensor's internal
     * bus serialises them. */
    for (size_t i = 0; i < GOODIX_IMAGE_PIXELS / 4; i++) {
        *out++ = (uint16_t)(((chunk[0] & 0x0f) << 8) | chunk[1]);
        *out++ = (uint16_t)((chunk[3] << 4) | (chunk[0] >> 4));
        *out++ = (uint16_t)(((chunk[5] & 0x0f) << 8) | chunk[2]);
        *out++ = (uint16_t)((chunk[4] << 4) | (chunk[5] >> 4));
        chunk += 6;
    }
}

void goodix_image_crop(const uint16_t *pixels, uint16_t *out)
{
    for (int y = 0; y < GOODIX_IMAGE_HEIGHT; y++)
        memcpy(out + y * GOODIX_IMAGE_USEFUL_WIDTH,
               pixels + y * GOODIX_IMAGE_WIDTH,
               GOODIX_IMAGE_USEFUL_WIDTH * sizeof(*out));
}

void goodix_image_normalise(const uint16_t *pixels, uint8_t *out, size_t count)
{
    uint16_t min = 0xffff;
    uint16_t max = 0;

    for (size_t i = 0; i < count; i++) {
        if (pixels[i] < min)
            min = pixels[i];
        if (pixels[i] > max)
            max = pixels[i];
    }

    if (max == min) {
        memset(out, 0, count);
        return;
    }

    for (size_t i = 0; i < count; i++)
        out[i] = (uint8_t)((pixels[i] - min) * 255u / (max - min));
}

void goodix_image_subtract(const uint16_t *frame, const uint16_t *background,
                           int16_t *out, size_t count)
{
    for (size_t i = 0; i < count; i++)
        out[i] = (int16_t)((int)frame[i] - (int)background[i]);
}

void goodix_image_normalise_signed(const int16_t *pixels, uint8_t *out,
                                   size_t count)
{
    int min = 32767;
    int max = -32768;

    for (size_t i = 0; i < count; i++) {
        if (pixels[i] < min)
            min = pixels[i];
        if (pixels[i] > max)
            max = pixels[i];
    }

    if (max == min) {
        memset(out, 0, count);
        return;
    }

    for (size_t i = 0; i < count; i++)
        out[i] = (uint8_t)((pixels[i] - min) * 255 / (max - min));
}

void goodix_image_enhance(const uint8_t *in, uint8_t *out,
                          int width, int height, int window)
{
    const double target_mean = 128.0;
    const double target_sd   = 48.0;
    const int    half        = window / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            long   sum    = 0;
            long   sum_sq = 0;
            int    count  = 0;
            double mean;
            double variance;
            double sd;
            double scaled;

            for (int wy = y - half; wy <= y + half; wy++) {
                if (wy < 0 || wy >= height)
                    continue;

                for (int wx = x - half; wx <= x + half; wx++) {
                    int value;

                    if (wx < 0 || wx >= width)
                        continue;

                    value   = in[wy * width + wx];
                    sum    += value;
                    sum_sq += (long)value * value;
                    count++;
                }
            }

            mean     = (double)sum / count;
            variance = (double)sum_sq / count - mean * mean;
            sd       = variance > 1.0 ? sqrt(variance) : 1.0;

            scaled = target_mean
                   + (in[y * width + x] - mean) * target_sd / sd;

            if (scaled < 0.0)
                scaled = 0.0;
            if (scaled > 255.0)
                scaled = 255.0;

            out[y * width + x] = (uint8_t)scaled;
        }
    }
}

int goodix_image_write_pgm16(const char *path, const uint16_t *pixels,
                             int width, int height, uint16_t maxval)
{
    size_t count = (size_t)width * (size_t)height;
    FILE  *file  = fopen(path, "wb");

    if (file == NULL)
        return GOODIX_ERR_IO;

    fprintf(file, "P5\n%d %d\n%u\n", width, height, maxval);

    for (size_t i = 0; i < count; i++) {
        fputc(pixels[i] >> 8, file);
        fputc(pixels[i] & 0xff, file);
    }

    return fclose(file) == 0 ? GOODIX_OK : GOODIX_ERR_IO;
}

int goodix_image_write_pgm8(const char *path, const uint8_t *pixels,
                            int width, int height)
{
    size_t count = (size_t)width * (size_t)height;
    FILE  *file  = fopen(path, "wb");

    if (file == NULL)
        return GOODIX_ERR_IO;

    fprintf(file, "P5\n%d %d\n255\n", width, height);
    fwrite(pixels, 1, count, file);

    return fclose(file) == 0 ? GOODIX_OK : GOODIX_ERR_IO;
}
