/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goodix/session.h"

#define HANDSHAKE_ROUNDS 8
#define RECORD_MAX       4096

static const uint8_t payload_none[2] = { 0x00, 0x00 };

static void trace_record(const goodix_session *session, const char *label,
                         const uint8_t *data, int len)
{
    if (!session->trace)
        return;

    printf("  %-6s -> %4d bytes  [%s]", label, len,
           goodix_tls_record_name(data[0]));

    if (data[0] == 0x15 && len >= 7)
        printf(" level %u, %s (%u)",
               data[5], goodix_tls_alert_name(data[6]), data[6]);

    printf("\n");
}

/* Wraps whatever OpenSSL wants to send in a TLS envelope and puts it on the
 * wire. Returns the number of bytes sent, 0 if there were none. */
static int flush_records(goodix_session *session)
{
    uint8_t payload[RECORD_MAX];
    uint8_t frame[RECORD_MAX + GOODIX_MAX_PACKET];
    size_t  frame_len;
    int     n;
    int     rc;

    n = goodix_tls_take(&session->tls, payload, sizeof(payload));
    if (n <= 0)
        return 0;

    trace_record(session, "host", payload, n);

    rc = goodix_encode_pack(GOODIX_FLAG_TLS, payload, (uint16_t)n,
                            frame, sizeof(frame), &frame_len);
    if (rc < 0)
        return rc;

    rc = goodix_write(session->dev, frame, frame_len);
    return rc < 0 ? rc : n;
}

/* Reads one envelope and hands its contents to OpenSSL. */
static int absorb_record(goodix_session *session)
{
    const uint8_t *body;
    uint16_t       body_len;
    uint8_t        flags;
    int            rc;

    rc = goodix_receive_pack(session->dev, session->frame,
                             sizeof(session->frame), &flags, &body, &body_len);
    if (rc < 0)
        return rc;

    if (flags != GOODIX_FLAG_TLS || body_len == 0)
        return GOODIX_ERR_PROTOCOL;

    trace_record(session, "sensor", body, body_len);

    return goodix_tls_feed(&session->tls, body, body_len) < 0
               ? GOODIX_ERR_TLS
               : (int)body_len;
}

static int negotiate(goodix_session *session)
{
    int rc;

    rc = goodix_command(session->dev, GOODIX_CMD_REQUEST_TLS_CONNECTION,
                        payload_none, sizeof(payload_none));
    if (rc < 0)
        return rc;

    /* The sensor is the TLS client, so it speaks first. */
    rc = absorb_record(session);
    if (rc < 0)
        return rc;

    for (int round = 0; round < HANDSHAKE_ROUNDS; round++) {
        int step = goodix_tls_handshake_step(&session->tls);

        /* Flush before judging: a failed step still leaves an alert to send. */
        rc = flush_records(session);
        if (rc < 0)
            return rc;

        if (step == 1)
            return GOODIX_OK;
        if (step < 0)
            return GOODIX_ERR_TLS;

        rc = absorb_record(session);
        if (rc < 0)
            return rc;
    }

    return GOODIX_ERR_TLS;
}

int goodix_session_open(goodix_session *session, goodix_device *dev,
                        const char *psk_path, int trace)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE] = { 0 };
    int     rc;

    memset(session, 0, sizeof(*session));
    session->dev   = dev;
    session->trace = trace;

    if (psk_path != NULL) {
        rc = goodix_tls_load_psk(psk_path, psk, sizeof(psk));
    } else {
        static const char *const fallbacks[] = {
            GOODIX_PSK_PATH_SYSTEM,
            GOODIX_PSK_PATH_LOCAL,
        };
        const char *from_env = getenv("GOODIX_PSK_FILE");

        rc = GOODIX_ERR_IO;

        if (from_env != NULL) {
            rc = goodix_tls_load_psk(from_env, psk, sizeof(psk));
        } else {
            for (size_t i = 0; i < sizeof(fallbacks) / sizeof(*fallbacks); i++) {
                rc = goodix_tls_load_psk(fallbacks[i], psk, sizeof(psk));
                if (rc != GOODIX_ERR_IO)
                    break;              /* found it, good or malformed */
            }
        }
    }

    if (rc == GOODIX_ERR_BUFFER)
        return rc;                     /* file present but malformed */

    rc = goodix_initialise(dev, NULL, NULL, &session->report);
    if (rc < 0)
        goto done;

    rc = goodix_tls_init(&session->tls, GOODIX_TLS_SERVER, psk, sizeof(psk));
    if (rc < 0)
        goto done;

    rc = negotiate(session);
    if (rc < 0) {
        goodix_tls_free(&session->tls);
        goto done;
    }

    session->established = 1;

    /* Tells the sensor the channel is ready. It does not always acknowledge
     * this one, which is not fatal: the channel is already up. */
    if (goodix_command(dev, GOODIX_CMD_TLS_ESTABLISHED,
                       payload_none, sizeof(payload_none)) < 0)
        goodix_drain(dev);

    rc = GOODIX_OK;

done:
    explicit_bzero(psk, sizeof(psk));
    return rc;
}

void goodix_session_close(goodix_session *session)
{
    /* Always clear the channel state, whether the handshake succeeded or not.
     * The sensor keeps it across process exits and even power cycles, and a
     * sensor left waiting refuses every plain command — so the next open
     * would fail. See SPEC.md section 10. */
    if (session->dev && session->dev->handle) {
        goodix_drain(session->dev);
        if (goodix_command(session->dev, GOODIX_CMD_TLS_ESTABLISHED,
                           payload_none, sizeof(payload_none)) < 0)
            fprintf(stderr, "warning: could not clear the TLS state, "
                            "run build/unlock\n");
    }

    /* Safe on a session that never opened: the struct starts zeroed. */
    goodix_tls_free(&session->tls);
    session->established = 0;
}

int goodix_session_read(goodix_session *session, uint8_t *buf, size_t length)
{
    size_t got = 0;

    while (got < length) {
        int n = goodix_tls_read(&session->tls, buf + got, length - got);

        if (n < 0)
            return n;
        if (n > 0) {
            got += (size_t)n;
            continue;
        }

        n = absorb_record(session);
        if (n < 0)
            return n;
    }

    return (int)got;
}
