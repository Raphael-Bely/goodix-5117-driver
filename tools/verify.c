/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Checks one touch against a recorded finger.
 *
 * usage: verify [path]
 * exit status: 0 accepted, 1 rejected, 2 error */

#include <stdio.h>

#include "goodix/capture.h"
#include "goodix/match.h"

#define DEFAULT_PATH "finger.enrol"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : DEFAULT_PATH;

    goodix_device    dev;
    goodix_session   session;
    goodix_enrolment enrolment;
    goodix_template  probe;
    uint8_t          background[GOODIX_IMAGE_RAW_SIZE];
    uint8_t          finger[GOODIX_IMAGE_RAW_SIZE];
    double           score;
    int              rc;
    int              status = 2;

    setvbuf(stdout, NULL, _IONBF, 0);

    rc = goodix_enrolment_load(path, &enrolment);
    if (rc < 0) {
        fprintf(stderr, "cannot read %s: %s\n", path, goodix_strerror(rc));
        fprintf(stderr, "run build/enroll first\n");
        return 2;
    }

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 2;
    }

    rc = goodix_session_open(&session, &dev, NULL, 0);
    if (rc < 0) {
        fprintf(stderr, "session: %s (%s)\n",
                goodix_strerror(rc), goodix_tls_last_error());
        goto out;
    }

    rc = goodix_capture_prepare(&session);
    if (rc < 0)
        goto fail;

    rc = goodix_capture_frame(&session, background);
    if (rc < 0)
        goto fail;

    printf("touch the sensor\n");
    rc = goodix_capture_wait_finger(&session, GOODIX_FINGER_TIMEOUT_MS);
    if (rc < 0)
        goto fail;

    rc = goodix_capture_frame(&session, finger);
    if (rc < 0)
        goto fail;

    goodix_capture_wait_lift(&session, GOODIX_FINGER_TIMEOUT_MS);

    goodix_match_prepare(background, finger, &probe);
    score = goodix_enrolment_score(&enrolment, &probe);

    printf("\nscore %.3f against %u templates, threshold %.2f\n",
           score, enrolment.count, GOODIX_MATCH_THRESHOLD);

    if (score >= GOODIX_MATCH_THRESHOLD) {
        printf("ACCEPTED\n");
        status = 0;
    } else {
        printf("REJECTED\n");
        status = 1;
    }
    goto out;

fail:
    fprintf(stderr, "capture: %s\n", goodix_strerror(rc));

out:
    goodix_session_close(&session);
    goodix_close(&dev);
    return status;
}
