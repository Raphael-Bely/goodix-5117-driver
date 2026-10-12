/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Asks the sensor to open an encrypted channel and reports whatever comes
 * back, without assuming a shape. Nothing is written to the sensor.
 *
 * It leaves the sensor waiting for a ServerHello, so it sends 0xd4 on the way
 * out to clear that state. See SPEC.md section 10. */

#include <stdio.h>
#include <string.h>

#include "goodix/init.h"

#define PROBE_FRAMES 4

static const char *tls_suite_name(uint16_t id)
{
    switch (id) {
    case 0x002f: return "TLS_RSA_WITH_AES_128_CBC_SHA";
    case 0x0035: return "TLS_RSA_WITH_AES_256_CBC_SHA";
    case 0x008c: return "TLS_PSK_WITH_AES_128_CBC_SHA";
    case 0x008d: return "TLS_PSK_WITH_AES_256_CBC_SHA";
    case 0x00a8: return "TLS_PSK_WITH_AES_128_GCM_SHA256";
    case 0x00a9: return "TLS_PSK_WITH_AES_256_GCM_SHA384";
    case 0x00ae: return "TLS_PSK_WITH_AES_128_CBC_SHA256";
    case 0x00af: return "TLS_PSK_WITH_AES_256_CBC_SHA384";
    case 0xc035: return "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA";
    case 0xc036: return "TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA";
    case 0xc037: return "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256";
    /* Not a cipher suite: signals support for secure renegotiation. */
    case 0x00ff: return "TLS_EMPTY_RENEGOTIATION_INFO_SCSV";
    default:     return "unknown";
    }
}

static void dump(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i += 16) {
        printf("    %04x  ", i);
        for (uint16_t j = i; j < i + 16 && j < len; j++)
            printf("%02x ", data[j]);
        printf("\n");
    }
}

static void describe_message(const uint8_t *body, uint16_t len)
{
    uint16_t field;

    if (len < GOODIX_PROTOCOL_HEADER_SIZE + 1) {
        printf("    truncated command\n");
        return;
    }

    field = goodix_le16(body + 1);
    printf("    command 0x%02x, %u byte(s) of payload\n",
           body[0], field ? (unsigned)(field - 1) : 0u);

    if (body[0] == GOODIX_CMD_ACK && len >= 5)
        printf("    ACK for 0x%02x, flags 0x%02x\n", body[3], body[4]);
}

static void describe_tls(const uint8_t *data, uint16_t len)
{
    uint16_t suites_len;
    size_t   p;

    if (len < 6 || data[0] != 0x16) {
        printf("    not a TLS handshake record\n");
        return;
    }

    /* TLS is big-endian, unlike the Goodix protocol. */
    printf("    TLS handshake record, version %u.%u, body %u bytes\n",
           data[1], data[2], (uint16_t)((data[3] << 8) | data[4]));

    if (data[5] != 0x01) {
        printf("    handshake type 0x%02x, not a ClientHello\n", data[5]);
        return;
    }

    if (len < 44) {
        printf("    ClientHello, truncated before the session id\n");
        return;
    }

    printf("    ClientHello, client version %u.%u\n", data[9], data[10]);

    p = 43;               /* session id length */
    p += 1 + data[p];     /* skip the session id itself */
    if (p + 2 > len)
        return;

    suites_len = (uint16_t)((data[p] << 8) | data[p + 1]);
    p += 2;
    if (p + suites_len > len)
        return;

    printf("    %u cipher suite(s) offered:\n", suites_len / 2);
    for (uint16_t i = 0; i + 1 < suites_len; i += 2) {
        uint16_t id = (uint16_t)((data[p + i] << 8) | data[p + i + 1]);
        printf("      0x%04x  %s\n", id, tls_suite_name(id));
    }
}

int main(void)
{
    const uint8_t payload[2] = { 0x00, 0x00 };

    goodix_device dev;
    uint8_t       buf[GOODIX_MAX_RESPONSE];
    int           rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    printf("initialising sensor\n");
    rc = goodix_initialise(&dev, NULL, NULL, NULL);
    if (rc < 0) {
        fprintf(stderr, "initialise: %s\n", goodix_strerror(rc));
        goodix_close(&dev);
        return 1;
    }

    printf("sending REQUEST_TLS_CONNECTION (0xd0)\n\n");
    rc = goodix_send(&dev, GOODIX_CMD_REQUEST_TLS_CONNECTION,
                     payload, sizeof(payload), GOODIX_CHECKSUM_COMPUTED);
    if (rc < 0) {
        fprintf(stderr, "send: %s\n", goodix_strerror(rc));
        goodix_close(&dev);
        return 1;
    }

    for (int i = 1; i <= PROBE_FRAMES; i++) {
        const uint8_t *body;
        uint16_t       body_len;
        uint8_t        flags;

        rc = goodix_receive_pack(&dev, buf, sizeof(buf),
                                 &flags, &body, &body_len);
        if (rc < 0) {
            printf("frame %d: %s\n", i, goodix_strerror(rc));
            break;
        }

        printf("frame %d: envelope 0x%02x, %u bytes\n", i, flags, body_len);
        dump(body, body_len);

        if (flags == GOODIX_FLAG_MESSAGE)
            describe_message(body, body_len);
        else if (flags == GOODIX_FLAG_TLS)
            describe_tls(body, body_len);
        else
            printf("    unknown envelope type\n");

        printf("\n");
    }

    goodix_drain(&dev);
    if (goodix_command(&dev, GOODIX_CMD_TLS_ESTABLISHED, payload, sizeof(payload)) < 0)
        fprintf(stderr, "warning: could not clear TLS state; run build/unlock\n");

    goodix_close(&dev);
    return 0;
}
