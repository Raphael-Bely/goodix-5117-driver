/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <unistd.h>

#include "goodix/image.h"
#include "test.h"

#define TMP_PGM "/tmp/goodix-test.pgm"

/* Six bytes carry four samples, interleaved. Worked out by hand from the
 * formula in SPEC.md section 8, so this catches a transcription slip. */
static void unpacks_twelve_bit_samples (void)
{
    uint8_t  raw[GOODIX_IMAGE_RAW_SIZE];
    uint16_t pixels[GOODIX_IMAGE_PIXELS];

    memset (raw, 0, sizeof (raw));
    raw[GOODIX_IMAGE_HEAD + 0] = 0x12;
    raw[GOODIX_IMAGE_HEAD + 1] = 0x34;
    raw[GOODIX_IMAGE_HEAD + 2] = 0x56;
    raw[GOODIX_IMAGE_HEAD + 3] = 0x78;
    raw[GOODIX_IMAGE_HEAD + 4] = 0x9a;
    raw[GOODIX_IMAGE_HEAD + 5] = 0xbc;

    goodix_image_decode (raw, pixels);

    TEST_EQ (pixels[0], 0x234);
    TEST_EQ (pixels[1], 0x781);
    TEST_EQ (pixels[2], 0xc56);
    TEST_EQ (pixels[3], 0x9ab);
}

/* The header must be skipped, not decoded. */
static void ignores_the_frame_header (void)
{
    uint8_t  raw[GOODIX_IMAGE_RAW_SIZE];
    uint16_t pixels[GOODIX_IMAGE_PIXELS];

    memset (raw, 0, sizeof (raw));
    memset (raw, 0xff, GOODIX_IMAGE_HEAD);

    goodix_image_decode (raw, pixels);

    TEST_EQ (pixels[0], 0);
    TEST_EQ (pixels[GOODIX_IMAGE_PIXELS - 1], 0);
}

static void crop_keeps_the_left_columns (void)
{
    uint16_t full[GOODIX_IMAGE_PIXELS];
    uint16_t cropped[GOODIX_IMAGE_USEFUL_PIXELS];

    for (int y = 0; y < GOODIX_IMAGE_HEIGHT; y++)
        for (int x = 0; x < GOODIX_IMAGE_WIDTH; x++)
            full[y * GOODIX_IMAGE_WIDTH + x] = (uint16_t) (y * 100 + x);

    goodix_image_crop (full, cropped);

    TEST_EQ (cropped[0], 0);
    TEST_EQ (cropped[GOODIX_IMAGE_USEFUL_WIDTH - 1],
             GOODIX_IMAGE_USEFUL_WIDTH - 1);
    /* Second row must start at the 88th sample, not the 64th. */
    TEST_EQ (cropped[GOODIX_IMAGE_USEFUL_WIDTH], 100);
}

static void normalise_stretches_to_the_full_range (void)
{
    uint16_t pixels[4] = { 100, 200, 300, 400 };
    uint8_t  out[4];

    goodix_image_normalise (pixels, out, 4);

    TEST_EQ (out[0], 0);
    TEST_EQ (out[3], 255);
    TEST_TRUE (out[1] > out[0] && out[2] > out[1]);
}

/* A flat image has no minimum to subtract from; it must not divide by zero. */
static void normalise_survives_a_flat_image (void)
{
    uint16_t pixels[4] = { 42, 42, 42, 42 };
    uint8_t  out[4]    = { 9, 9, 9, 9 };

    goodix_image_normalise (pixels, out, 4);

    for (int i = 0; i < 4; i++)
        TEST_EQ (out[i], 0);
}

static void subtract_keeps_the_sign (void)
{
    uint16_t frame[3]      = { 100, 200, 50 };
    uint16_t background[3] = { 150, 150, 150 };
    int16_t  out[3];

    goodix_image_subtract (frame, background, out, 3);

    TEST_EQ (out[0], -50);
    TEST_EQ (out[1], 50);
    TEST_EQ (out[2], -100);
}

static void signed_normalise_maps_the_extremes (void)
{
    int16_t values[3] = { -100, 0, 100 };
    uint8_t out[3];

    goodix_image_normalise_signed (values, out, 3);

    TEST_EQ (out[0], 0);
    TEST_EQ (out[2], 255);
    TEST_TRUE (out[1] > 100 && out[1] < 155);
}

/* Local equalisation recentres every pixel on its neighbourhood, so a flat
 * input must come out flat at the target mean. */
static void enhance_recentres_a_flat_image (void)
{
    uint8_t flat[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t out[GOODIX_IMAGE_USEFUL_PIXELS];

    memset (flat, 70, sizeof (flat));
    goodix_image_enhance (flat, out, GOODIX_IMAGE_USEFUL_WIDTH,
                          GOODIX_IMAGE_HEIGHT, GOODIX_ENHANCE_WINDOW);

    TEST_EQ (out[0], 128);
    TEST_EQ (out[GOODIX_IMAGE_USEFUL_PIXELS / 2], 128);
}

static void enhance_lifts_a_dim_corner (void)
{
    uint8_t in[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t out[GOODIX_IMAGE_USEFUL_PIXELS];
    int     bright_swing;
    int     dim_swing;

    /* Alternating ridges, but with half the contrast on the right — the
     * vignetting this step exists to cancel. */
    for (int y = 0; y < GOODIX_IMAGE_HEIGHT; y++)
        for (int x = 0; x < GOODIX_IMAGE_USEFUL_WIDTH; x++)
            {
                int swing = (x < GOODIX_IMAGE_USEFUL_WIDTH / 2) ? 60 : 15;

                in[y * GOODIX_IMAGE_USEFUL_WIDTH + x] =
                    (uint8_t) (128 + ((y % 6 < 3) ? swing : -swing));
            }

    goodix_image_enhance (in, out, GOODIX_IMAGE_USEFUL_WIDTH,
                          GOODIX_IMAGE_HEIGHT, GOODIX_ENHANCE_WINDOW);

    bright_swing = abs (out[10 * GOODIX_IMAGE_USEFUL_WIDTH + 10] - 128);
    dim_swing    = abs (out[10 * GOODIX_IMAGE_USEFUL_WIDTH + 55] - 128);

    /* Both sides should now swing by a comparable amount. */
    TEST_TRUE (dim_swing > bright_swing / 2);
}

static void writes_an_eight_bit_pgm (void)
{
    uint8_t pixels[4] = { 0, 85, 170, 255 };
    uint8_t file[64];
    FILE   *handle;
    size_t  len;

    TEST_EQ (goodix_image_write_pgm8 (TMP_PGM, pixels, 2, 2), GOODIX_OK);

    handle = fopen (TMP_PGM, "rb");
    TEST_TRUE (handle != NULL);
    len = fread (file, 1, sizeof (file), handle);
    fclose (handle);
    unlink (TMP_PGM);

    TEST_EQ (len, 11 + 4);            /* "P5\n2 2\n255\n" then the samples */
    TEST_MEM (file, "P5\n2 2\n255\n", 11);
    TEST_MEM (file + 11, pixels, 4);
}

/* PGM stores 16-bit samples most significant byte first — the opposite of
 * the Goodix protocol, which is exactly the kind of thing to get wrong. */
static void writes_sixteen_bit_pgm_big_endian (void)
{
    uint16_t pixels[2] = { 0x0123, 0x0456 };
    uint8_t  file[64];
    FILE    *handle;
    size_t   len;

    TEST_EQ (goodix_image_write_pgm16 (TMP_PGM, pixels, 2, 1, 4095),
             GOODIX_OK);

    handle = fopen (TMP_PGM, "rb");
    TEST_TRUE (handle != NULL);
    len = fread (file, 1, sizeof (file), handle);
    fclose (handle);
    unlink (TMP_PGM);

    TEST_EQ (len, 12 + 4);
    TEST_MEM (file, "P5\n2 1\n4095\n", 12);
    TEST_EQ (file[12], 0x01);
    TEST_EQ (file[13], 0x23);
    TEST_EQ (file[14], 0x04);
    TEST_EQ (file[15], 0x56);
}

int main (void)
{
    TEST_RUN (unpacks_twelve_bit_samples);
    TEST_RUN (ignores_the_frame_header);
    TEST_RUN (crop_keeps_the_left_columns);
    TEST_RUN (normalise_stretches_to_the_full_range);
    TEST_RUN (normalise_survives_a_flat_image);
    TEST_RUN (subtract_keeps_the_sign);
    TEST_RUN (signed_normalise_maps_the_extremes);
    TEST_RUN (enhance_recentres_a_flat_image);
    TEST_RUN (enhance_lifts_a_dim_corner);
    TEST_RUN (writes_an_eight_bit_pgm);
    TEST_RUN (writes_sixteen_bit_pgm_big_endian);

    TEST_SUMMARY ("image");
}
