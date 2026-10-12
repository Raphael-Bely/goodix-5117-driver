/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Clears the "waiting for a TLS handshake" state a sensor falls into after an
 * unfinished 0xd0. That state survives power cycles; 0xd4 clears it. */

#include <stdio.h>

#include "goodix/protocol.h"

int main(void)
{
    const uint8_t none[2] = { 0x00, 0x00 };

    goodix_device  dev;
    goodix_message reply;
    uint8_t        buf[GOODIX_MAX_RESPONSE];
    int            rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    goodix_drain(&dev);

    rc = goodix_command(&dev, GOODIX_CMD_TLS_ESTABLISHED, none, sizeof(none));
    printf("0xd4: %s\n", rc < 0 ? goodix_strerror(rc) : "acknowledged");

    goodix_drain(&dev);

    rc = goodix_command_reply(&dev, GOODIX_CMD_FIRMWARE_VERSION,
                              none, sizeof(none), buf, sizeof(buf), &reply);
    if (rc < 0) {
        printf("still stuck: %s\n", goodix_strerror(rc));
        goodix_close(&dev);
        return 1;
    }

    printf("sensor responding: %.*s\n", (int)reply.payload_len, reply.payload);

    goodix_close(&dev);
    return 0;
}
