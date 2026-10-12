/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Collects a batch of captures into samples/, to measure how scores behave
 * on the same finger and on different ones.
 *
 * usage: sample <label> <count> */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "goodix/capture.h"
#include "goodix/image.h"

#define SAMPLE_DIR "samples"

static int save_raw(const char *path, const uint8_t *raw)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }

    fwrite(raw, 1, GOODIX_IMAGE_RAW_SIZE, file);
    fclose(file);

    return 0;
}

static int take_one(goodix_session *session, const char *label, int index)
{
    uint8_t background[GOODIX_IMAGE_RAW_SIZE];
    uint8_t finger[GOODIX_IMAGE_RAW_SIZE];
    char    path[256];
    int     rc;

    rc = goodix_capture_frame(session, background);
    if (rc < 0)
        return rc;

    printf("  press");
    fflush(stdout);

    rc = goodix_capture_wait_finger(session, GOODIX_FINGER_TIMEOUT_MS);
    if (rc < 0)
        return rc;

    rc = goodix_capture_frame(session, finger);
    if (rc < 0)
        return rc;

    snprintf(path, sizeof(path), SAMPLE_DIR "/%s-%02d-bg.bin", label, index);
    if (save_raw(path, background) < 0)
        return GOODIX_ERR_IO;

    snprintf(path, sizeof(path), SAMPLE_DIR "/%s-%02d-fp.bin", label, index);
    if (save_raw(path, finger) < 0)
        return GOODIX_ERR_IO;

    printf(" ... lift");
    fflush(stdout);

    rc = goodix_capture_wait_lift(session, GOODIX_FINGER_TIMEOUT_MS);
    if (rc < 0)
        return rc;

    printf(" ... saved\n");
    return GOODIX_OK;
}

int main(int argc, char **argv)
{
    goodix_device  dev;
    goodix_session session;
    const char    *label;
    int            count;
    int            taken  = 0;
    int            status = 1;
    int            rc;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <label> <count>\n\n"
                        "  %s index 15     the finger you will unlock with\n"
                        "  %s middle 8     a different finger, for comparison\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }

    label = argv[1];
    count = atoi(argv[2]);
    if (count < 1 || count > 99) {
        fprintf(stderr, "count must be between 1 and 99\n");
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    mkdir(SAMPLE_DIR, 0755);

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

    printf("collecting %d samples labelled \"%s\"\n", count, label);
    printf("place the finger the way you naturally would to unlock: same\n"
           "spot, same angle. The sensor sees only a few square millimetres,\n"
           "so a deliberate shift lands on a different patch of skin and\n"
           "nothing can match it. Natural variation is enough.\n\n");

    for (int i = 1; i <= count; i++) {
        printf("%2d/%d:", i, count);

        rc = take_one(&session, label, i);
        if (rc < 0) {
            fprintf(stderr, " failed: %s\n", goodix_strerror(rc));
            break;
        }
        taken++;
    }

    printf("\n%d of %d samples written to %s/\n", taken, count, SAMPLE_DIR);
    status = (taken == count) ? 0 : 1;

out:
    goodix_session_close(&session);
    goodix_close(&dev);
    return status;
}
