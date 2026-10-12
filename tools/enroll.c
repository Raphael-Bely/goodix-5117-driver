/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Records a finger: several touches, stored as templates.
 *
 * usage: enroll [count] [path] */

#include <stdio.h>
#include <stdlib.h>

#include "goodix/capture.h"
#include "goodix/match.h"

#define DEFAULT_PATH  "finger.enrol"
#define DEFAULT_COUNT 8
#define MAX_RETRIES   4

int main(int argc, char **argv)
{
    int         count = (argc > 1) ? atoi(argv[1]) : DEFAULT_COUNT;
    const char *path  = (argc > 2) ? argv[2] : DEFAULT_PATH;

    goodix_device     dev;
    goodix_session    session;
    goodix_enrolment  enrolment = { 0, { { { 0 } } } };
    uint8_t           background[GOODIX_IMAGE_RAW_SIZE];
    uint8_t           finger[GOODIX_IMAGE_RAW_SIZE];
    int               rc;
    int               status = 1;

    if (count < 1 || count > GOODIX_ENROL_MAX) {
        fprintf(stderr, "count must be between 1 and %d\n", GOODIX_ENROL_MAX);
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    rc = goodix_session_open(&session, &dev, NULL, 0);
    if (rc < 0) {
        fprintf(stderr, "session: %s (%s)\n",
                goodix_strerror(rc), goodix_tls_last_error());
        goto out;
    }

    rc = goodix_capture_prepare(&session);
    if (rc < 0) {
        fprintf(stderr, "prepare: %s\n", goodix_strerror(rc));
        goto out;
    }

    printf("enrolling %d touches\n\n", count);
    printf("Put the same part of the finger down every time, at the same\n"
           "angle. The sensor sees a few square millimetres: moving between\n"
           "touches records a different patch of skin, and the enrolment\n"
           "ends up describing several fingers instead of one.\n\n");

    for (int i = 0; i < count; i++) {
        goodix_template candidate;
        double          agreement = 1.0;
        int             retries   = 0;
        int             keep      = 1;

        for (;;) {
            printf("%d/%d: press", i + 1, count);

            rc = goodix_capture_frame(&session, background);
            if (rc < 0)
                goto fail;

            rc = goodix_capture_wait_finger(&session,
                                            GOODIX_FINGER_TIMEOUT_MS);
            if (rc < 0)
                goto fail;

            rc = goodix_capture_frame(&session, finger);
            if (rc < 0)
                goto fail;

            goodix_match_prepare(background, finger, &candidate);

            printf(" ... lift");
            rc = goodix_capture_wait_lift(&session, GOODIX_FINGER_TIMEOUT_MS);
            if (rc < 0)
                goto fail;

            /* The first touch defines the spot; the rest must match it. */
            if (i > 0)
                agreement = goodix_enrolment_score(&enrolment, &candidate);

            if (agreement >= GOODIX_ENROL_MIN_AGREEMENT)
                break;

            if (retries >= MAX_RETRIES) {
                /* A template that matches none of the others adds false
                 * accept risk without covering anything useful. Dropping it
                 * is strictly better than keeping it. */
                keep = 0;
                break;
            }

            printf(" ... only %.2f, too far from the others, again\n",
                   agreement);
            retries++;
        }

        if (!keep) {
            printf(" ... dropped, never matched the others (%.2f)\n",
                   agreement);
            continue;
        }

        enrolment.entries[enrolment.count] = candidate;
        enrolment.count++;

        if (i == 0)
            printf(" ... kept, this one sets the spot\n");
        else
            printf(" ... kept, agreement %.2f\n", agreement);
    }

    if (enrolment.count == 0) {
        fprintf(stderr, "\nnothing usable was recorded\n");
        goto out;
    }

    rc = goodix_enrolment_save(path, &enrolment);
    if (rc < 0) {
        fprintf(stderr, "cannot write %s: %s\n", path, goodix_strerror(rc));
        goto out;
    }

    printf("\nwrote %s with %u templates\n", path, enrolment.count);

    /* How well the kept templates agree tells whether verification will be
     * comfortable or marginal. */
    {
        double worst = 2.0;
        double sum   = 0.0;
        int    pairs = 0;

        for (uint32_t a = 0; a < enrolment.count; a++)
            for (uint32_t b = a + 1; b < enrolment.count; b++) {
                double v = goodix_match_score(&enrolment.entries[a],
                                              &enrolment.entries[b]);
                if (v < worst)
                    worst = v;
                sum += v;
                pairs++;
            }

        if (pairs > 0) {
            printf("internal agreement: mean %.2f, worst %.2f\n",
                   sum / pairs, worst);
            if (sum / pairs < 0.70)
                printf("that is low; enrol again keeping the finger still\n");
            else
                printf("coherent enrolment, impostors should stay well below "
                       "%.2f\n", GOODIX_MATCH_THRESHOLD);
        }
    }
    status = 0;
    goto out;

fail:
    fprintf(stderr, " failed: %s\n", goodix_strerror(rc));

out:
    goodix_session_close(&session);
    goodix_close(&dev);
    return status;
}
