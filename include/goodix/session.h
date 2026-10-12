/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_SESSION_H
#define GOODIX_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "goodix/init.h"
#include "goodix/tls.h"

/* Where the key of a Windows-paired sensor is looked for, in order:
 *
 *   1. the GOODIX_PSK_FILE environment variable
 *   2. GOODIX_PSK_PATH_SYSTEM, for a driver started by a daemon
 *   3. GOODIX_PSK_PATH_LOCAL, relative to the working directory
 *
 * A driver loaded by fprintd starts in an arbitrary directory, so the
 * relative path is a development convenience only. */
#define GOODIX_PSK_PATH_SYSTEM "/etc/goodix5117/psk"
#define GOODIX_PSK_PATH_LOCAL  "clef.txt"

/* An initialised sensor with an open encrypted channel. */
typedef struct {
    goodix_device *dev;
    goodix_tls     tls;
    int            trace;        /* print each record exchanged */
    goodix_init_report report;   /* what the initialisation learned */
    int            established;
    uint8_t        frame[GOODIX_MAX_RESPONSE];
} goodix_session;

/* Loads the key, runs the initialisation sequence, negotiates the channel and
 * confirms it with 0xd4. `psk_path` may be NULL to use the default. */
int goodix_session_open(goodix_session *session, goodix_device *dev,
                        const char *psk_path, int trace);

/* Clears the handshake state if it was left open. Always call this: a sensor
 * left waiting refuses plain commands, across power cycles. */
void goodix_session_close(goodix_session *session);

/* Reads encrypted frames off the wire until `length` plaintext bytes have
 * been recovered. */
int goodix_session_read(goodix_session *session, uint8_t *buf, size_t length);

#endif /* GOODIX_SESSION_H */
