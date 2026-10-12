/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Rebuilds the fingerprint from two saved frames, without touching hardware.
 * Everything after the capture can be reworked here and rerun instantly. */

#include <stdio.h>

#include "goodix/image.h"

static int load(const char *path, uint16_t *useful)
{
    uint8_t  raw[GOODIX_IMAGE_RAW_SIZE];
    uint16_t pixels[GOODIX_IMAGE_PIXELS];
    FILE    *file = fopen(path, "rb");
    size_t   len;

    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }

    len = fread(raw, 1, sizeof(raw), file);
    fclose(file);

    if (len != sizeof(raw)) {
        fprintf(stderr, "%s holds %zu bytes, expected %d\n",
                path, len, GOODIX_IMAGE_RAW_SIZE);
        return -1;
    }

    goodix_image_decode(raw, pixels);
    goodix_image_crop(pixels, useful);

    return 0;
}

int main(int argc, char **argv)
{
    const char *background_path = (argc > 1) ? argv[1] : "frame-background.bin";
    const char *finger_path     = (argc > 2) ? argv[2] : "frame-finger.bin";

    uint16_t background[GOODIX_IMAGE_USEFUL_PIXELS];
    uint16_t finger[GOODIX_IMAGE_USEFUL_PIXELS];
    int16_t  difference[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t  plain[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t  enhanced[GOODIX_IMAGE_USEFUL_PIXELS];

    if (load(background_path, background) < 0 ||
        load(finger_path, finger) < 0)
        return 1;

    goodix_image_subtract(finger, background, difference,
                          GOODIX_IMAGE_USEFUL_PIXELS);
    goodix_image_normalise_signed(difference, plain,
                                  GOODIX_IMAGE_USEFUL_PIXELS);
    goodix_image_enhance(plain, enhanced, GOODIX_IMAGE_USEFUL_WIDTH,
                         GOODIX_IMAGE_HEIGHT, GOODIX_ENHANCE_WINDOW);

    goodix_image_write_pgm8("fingerprint.pgm", plain,
                            GOODIX_IMAGE_USEFUL_WIDTH, GOODIX_IMAGE_HEIGHT);
    goodix_image_write_pgm8("fingerprint-enhanced.pgm", enhanced,
                            GOODIX_IMAGE_USEFUL_WIDTH, GOODIX_IMAGE_HEIGHT);

    printf("wrote fingerprint.pgm and fingerprint-enhanced.pgm\n");
    return 0;
}
