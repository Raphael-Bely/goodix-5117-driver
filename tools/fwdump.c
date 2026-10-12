/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Probes READ_FIRMWARE (0xf2) and, if the sensor answers, dumps what it
 * returns to a file.
 *
 * Read-only: this command cannot erase or modify anything. It is however
 * undocumented for this chip family and never exercised by the reference
 * tooling, so the sensor may simply ignore it. */

#include <stdio.h>
#include <string.h>

#include "goodix/protocol.h"

#define PROBE_LENGTH 64
#define CHUNK_LENGTH 512
#define DUMP_LIMIT   (128 * 1024)
#define DUMP_PATH    "firmware-dump.bin"

static const uint8_t payload_none[2] = { 0x00, 0x00 };
static const uint8_t payload_nop[4]  = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t payload_enable[2] = { 0x01, 0x00 };

/* Minimal wake-up: the reference tooling does exactly this before touching
 * any firmware command, rather than the full ten-step sequence. */
static int wake(goodix_device *dev)
{
    int rc;

    rc = goodix_send(dev, GOODIX_CMD_NOP, payload_nop, sizeof(payload_nop),
                     GOODIX_CHECKSUM_NULL);
    if (rc < 0)
        return rc;

    rc = goodix_command(dev, GOODIX_CMD_ENABLE_CHIP,
                        payload_enable, sizeof(payload_enable));
    if (rc < 0)
        return rc;

    return goodix_send(dev, GOODIX_CMD_NOP, payload_nop, sizeof(payload_nop),
                       GOODIX_CHECKSUM_NULL);
}

static int read_chunk(goodix_device *dev, uint32_t offset, uint32_t length,
                      uint8_t *buf, size_t buf_size, goodix_message *reply)
{
    const uint8_t request[8] = {
        (uint8_t)(offset        & 0xff), (uint8_t)((offset >>  8) & 0xff),
        (uint8_t)((offset >> 16) & 0xff), (uint8_t)((offset >> 24) & 0xff),
        (uint8_t)(length        & 0xff), (uint8_t)((length >>  8) & 0xff),
        (uint8_t)((length >> 16) & 0xff), (uint8_t)((length >> 24) & 0xff),
    };

    return goodix_command_reply(dev, GOODIX_CMD_READ_FIRMWARE,
                                request, sizeof(request),
                                buf, buf_size, reply);
}

int main(void)
{
    goodix_device  dev;
    goodix_message reply;
    uint8_t        buf[GOODIX_MAX_RESPONSE];
    FILE          *out = NULL;
    uint32_t       offset = 0;
    int            rc;
    int            status = 1;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    printf("waking sensor\n");
    rc = wake(&dev);
    if (rc < 0) {
        fprintf(stderr, "wake: %s\n", goodix_strerror(rc));
        goto out;
    }

    rc = goodix_command_reply(&dev, GOODIX_CMD_FIRMWARE_VERSION,
                              payload_none, sizeof(payload_none),
                              buf, sizeof(buf), &reply);
    if (rc < 0) {
        fprintf(stderr, "firmware version: %s\n", goodix_strerror(rc));
        goto out;
    }
    printf("running %.*s\n\n", (int)reply.payload_len, reply.payload);

    printf("probing READ_FIRMWARE (0xf2) for %d bytes at offset 0\n",
           PROBE_LENGTH);
    rc = read_chunk(&dev, 0, PROBE_LENGTH, buf, sizeof(buf), &reply);
    if (rc < 0) {
        printf("\nnot supported: %s\n", goodix_strerror(rc));
        printf("This firmware does not answer 0xf2. There is no way to back\n"
               "up the running image, so erasing it would be irreversible.\n");
        goto out;
    }

    printf("answered with %u bytes:\n", reply.payload_len);
    for (uint16_t i = 0; i < reply.payload_len && i < 64; i += 16) {
        printf("  %04x  ", i);
        for (uint16_t j = i; j < i + 16 && j < reply.payload_len; j++)
            printf("%02x ", reply.payload[j]);
        printf("\n");
    }

    if (reply.payload_len < PROBE_LENGTH) {
        printf("\nshort answer, not a usable dump\n");
        goto out;
    }

    printf("\ndumping to %s\n", DUMP_PATH);
    out = fopen(DUMP_PATH, "wb");
    if (out == NULL) {
        fprintf(stderr, "cannot open %s\n", DUMP_PATH);
        goto out;
    }

    while (offset < DUMP_LIMIT) {
        rc = read_chunk(&dev, offset, CHUNK_LENGTH, buf, sizeof(buf), &reply);
        if (rc < 0) {
            printf("stopped at offset %u: %s\n", offset, goodix_strerror(rc));
            break;
        }
        if (reply.payload_len == 0)
            break;

        fwrite(reply.payload, 1, reply.payload_len, out);
        offset += reply.payload_len;

        if (offset % (16 * 1024) == 0)
            printf("  %u bytes\n", offset);
    }

    printf("\nwrote %u bytes to %s\n", offset, DUMP_PATH);
    status = 0;

out:
    if (out)
        fclose(out);
    goodix_close(&dev);
    return status;
}
