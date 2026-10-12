/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "goodix/match.h"
#include "test.h"

#define TMP_ENROL "/tmp/goodix-test.enrol"

/* A deterministic texture. Random enough that a shifted copy is the only
 * thing it correlates with, reproducible so a failure is reproducible too. */
static void fill_pattern (goodix_template *tmpl, uint32_t seed)
{
    uint32_t state = seed;

    for (int i = 0; i < GOODIX_IMAGE_USEFUL_PIXELS; i++)
        {
            state = state * 1103515245u + 12345u;
            tmpl->pixels[i] = (uint8_t) (state >> 16);
        }
}

/* out[y][x] = in[y + dy][x + dx], zero outside. Matching the convention of
 * goodix_match_score, this is found at offset (dx, dy). */
static void fill_shifted (goodix_template *out, const goodix_template *in,
                          int dx, int dy)
{
    memset (out->pixels, 0, sizeof (out->pixels));

    for (int y = 0; y < GOODIX_IMAGE_HEIGHT; y++)
        for (int x = 0; x < GOODIX_IMAGE_USEFUL_WIDTH; x++)
            {
                int sy = y + dy;
                int sx = x + dx;

                if (sy >= 0 && sy < GOODIX_IMAGE_HEIGHT &&
                    sx >= 0 && sx < GOODIX_IMAGE_USEFUL_WIDTH)
                    out->pixels[y * GOODIX_IMAGE_USEFUL_WIDTH + x] =
                        in->pixels[sy * GOODIX_IMAGE_USEFUL_WIDTH + sx];
            }
}

/* The control that tells an algorithm bug apart from bad data. It is what
 * proved the matcher was sound when real scores looked hopeless. */
static void an_image_matches_itself (void)
{
    goodix_template a;

    fill_pattern (&a, 1);
    TEST_NEAR (goodix_match_score (&a, &a), 1.0, 0.0001);
}

static void a_shift_within_the_window_is_recovered (void)
{
    goodix_template a;
    goodix_template b;

    fill_pattern (&a, 2);
    fill_shifted (&b, &a, 5, 3);

    TEST_NEAR (goodix_match_score (&a, &b), 1.0, 0.0001);
}

static void a_shift_beyond_the_window_is_not (void)
{
    goodix_template a;
    goodix_template b;

    fill_pattern (&a, 3);
    fill_shifted (&b, &a, GOODIX_MATCH_SEARCH + 8, 0);

    TEST_TRUE (goodix_match_score (&a, &b) < 0.5);
}

static void unrelated_textures_score_low (void)
{
    goodix_template a;
    goodix_template b;

    fill_pattern (&a, 4);
    fill_pattern (&b, 5);

    TEST_TRUE (goodix_match_score (&a, &b) < 0.3);
}

/* Brightness and contrast must not move the score: a drier or harder-pressed
 * finger has to compare on the same footing. */
static void contrast_does_not_change_the_score (void)
{
    goodix_template a;
    goodix_template b;

    fill_pattern (&a, 6);

    for (int i = 0; i < GOODIX_IMAGE_USEFUL_PIXELS; i++)
        b.pixels[i] = (uint8_t) (a.pixels[i] / 2 + 60);

    TEST_NEAR (goodix_match_score (&a, &b), 1.0, 0.01);
}

static void enrolment_survives_a_round_trip (void)
{
    goodix_enrolment written = { 0, { { { 0 } } } };
    goodix_enrolment read;

    written.count = 3;
    for (uint32_t i = 0; i < written.count; i++)
        fill_pattern (&written.entries[i], 10 + i);

    TEST_EQ (goodix_enrolment_save (TMP_ENROL, &written), GOODIX_OK);
    TEST_EQ (goodix_enrolment_load (TMP_ENROL, &read), GOODIX_OK);
    TEST_EQ (read.count, written.count);
    TEST_MEM (read.entries[2].pixels, written.entries[2].pixels,
              GOODIX_IMAGE_USEFUL_PIXELS);

    unlink (TMP_ENROL);
}

/* Templates are biometric data; the file must not be readable by anyone else. */
static void enrolment_is_written_private (void)
{
    goodix_enrolment written = { 0, { { { 0 } } } };
    struct stat      info;

    written.count = 1;
    fill_pattern (&written.entries[0], 20);

    TEST_EQ (goodix_enrolment_save (TMP_ENROL, &written), GOODIX_OK);
    TEST_EQ (stat (TMP_ENROL, &info), 0);
    TEST_EQ (info.st_mode & 0777, 0600);

    unlink (TMP_ENROL);
}

static void enrolment_rejects_a_foreign_file (void)
{
    goodix_enrolment read;
    FILE            *handle = fopen (TMP_ENROL, "wb");
    const uint32_t   rubbish[3] = { 0xdeadbeef, 1, 1 };

    TEST_TRUE (handle != NULL);
    fwrite (rubbish, sizeof (rubbish), 1, handle);
    fclose (handle);

    TEST_EQ (goodix_enrolment_load (TMP_ENROL, &read), GOODIX_ERR_PROTOCOL);
    unlink (TMP_ENROL);
}

static void enrolment_rejects_a_missing_file (void)
{
    goodix_enrolment read;

    TEST_EQ (goodix_enrolment_load ("/tmp/goodix-does-not-exist", &read),
             GOODIX_ERR_IO);
}

/* A probe need only match one of the enrolled captures: a finger rarely
 * lands on the same spot twice. */
static void enrolment_score_takes_the_best (void)
{
    goodix_enrolment enrolment = { 0, { { { 0 } } } };
    goodix_template  probe;

    enrolment.count = 3;
    fill_pattern (&enrolment.entries[0], 30);
    fill_pattern (&enrolment.entries[1], 31);
    fill_pattern (&enrolment.entries[2], 32);

    fill_pattern (&probe, 31);   /* identical to the middle one */

    TEST_NEAR (goodix_enrolment_score (&enrolment, &probe), 1.0, 0.0001);
}

int main (void)
{
    TEST_RUN (an_image_matches_itself);
    TEST_RUN (a_shift_within_the_window_is_recovered);
    TEST_RUN (a_shift_beyond_the_window_is_not);
    TEST_RUN (unrelated_textures_score_low);
    TEST_RUN (contrast_does_not_change_the_score);
    TEST_RUN (enrolment_survives_a_round_trip);
    TEST_RUN (enrolment_is_written_private);
    TEST_RUN (enrolment_rejects_a_foreign_file);
    TEST_RUN (enrolment_rejects_a_missing_file);
    TEST_RUN (enrolment_score_takes_the_best);

    TEST_SUMMARY ("match");
}
