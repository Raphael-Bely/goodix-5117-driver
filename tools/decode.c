/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Turns a raw frame captured by build/capture into viewable PGM files.
 * Touches no hardware, so the image processing can be iterated on freely. */

#include <stdio.h>

#include "goodix/image.h"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "frame-raw.bin";

    uint8_t  raw[GOODIX_IMAGE_RAW_SIZE];
    uint16_t pixels[GOODIX_IMAGE_PIXELS];
    uint16_t useful[GOODIX_IMAGE_USEFUL_PIXELS];
    uint8_t  grey[GOODIX_IMAGE_USEFUL_PIXELS];
    uint16_t min = 0xffff;
    uint16_t max = 0;
    FILE    *file;
    size_t   read_len;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    read_len = fread(raw, 1, sizeof(raw), file);
    fclose(file);

    if (read_len != sizeof(raw)) {
        fprintf(stderr, "%s holds %zu bytes, expected %d\n",
                path, read_len, GOODIX_IMAGE_RAW_SIZE);
        return 1;
    }

    goodix_image_decode(raw, pixels);

    for (size_t i = 0; i < GOODIX_IMAGE_PIXELS; i++) {
        if (pixels[i] < min)
            min = pixels[i];
        if (pixels[i] > max)
            max = pixels[i];
    }

    printf("%d x %d, %d samples, values %u..%u\n",
           GOODIX_IMAGE_WIDTH, GOODIX_IMAGE_HEIGHT, GOODIX_IMAGE_PIXELS,
           min, max);

    goodix_image_crop(pixels, useful);
    goodix_image_normalise(useful, grey, GOODIX_IMAGE_USEFUL_PIXELS);

    if (goodix_image_write_pgm16("frame-12bit.pgm", pixels,
                                 GOODIX_IMAGE_WIDTH, GOODIX_IMAGE_HEIGHT,
                                 4095) < 0)
        fprintf(stderr, "cannot write frame-12bit.pgm\n");
    else
        printf("wrote frame-12bit.pgm   faithful, 16-bit samples\n");

    if (goodix_image_write_pgm8("frame-grey.pgm", grey,
                                GOODIX_IMAGE_USEFUL_WIDTH,
                                GOODIX_IMAGE_HEIGHT) < 0)
        fprintf(stderr, "cannot write frame-grey.pgm\n");
    else
        printf("wrote frame-grey.pgm    %d x %d, cropped and stretched\n",
               GOODIX_IMAGE_USEFUL_WIDTH, GOODIX_IMAGE_HEIGHT);

    return 0;
}
