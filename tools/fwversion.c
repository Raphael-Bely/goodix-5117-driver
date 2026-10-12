/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Sends a single FIRMWARE_VERSION command and prints the reply. */

#include <stdio.h>

#include "goodix/protocol.h"

int main(void)
{
    const uint8_t payload[2] = { 0x00, 0x00 };

    goodix_device  dev;
    goodix_message reply;
    uint8_t buf[GOODIX_MAX_RESPONSE];
    int     rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    rc = goodix_command_reply(&dev, GOODIX_CMD_FIRMWARE_VERSION,
                              payload, sizeof(payload),
                              buf, sizeof(buf), &reply);
    if (rc < 0) {
        fprintf(stderr, "firmware version: %s\n", goodix_strerror(rc));
        goodix_close(&dev);
        return 1;
    }

    printf("%.*s\n", (int)reply.payload_len, reply.payload);

    goodix_close(&dev);
    return 0;
}
