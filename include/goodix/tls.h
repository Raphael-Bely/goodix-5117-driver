/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_TLS_H
#define GOODIX_TLS_H

#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

#include "goodix/device.h"

/* The sensor offers exactly one suite; see SPEC.md section 6. */
#define GOODIX_TLS_CIPHER "PSK-AES128-GCM-SHA256"

/* The key the sensor expects in factory state. */
#define GOODIX_TLS_PSK_SIZE 32

typedef enum {
    GOODIX_TLS_SERVER,  /* our side: the host drives the handshake */
    GOODIX_TLS_CLIENT,  /* the sensor's side, used only to test offline */
} goodix_tls_role;

/* TLS endpoint with no transport of its own: bytes are handed in and taken
 * out explicitly, so the same code serves USB frames and in-memory tests. */
typedef struct {
    SSL_CTX        *ctx;
    SSL            *ssl;
    BIO            *rbio;  /* bytes received from the peer go in here */
    BIO            *wbio;  /* bytes to send to the peer come out of here */
    goodix_tls_role role;
    uint8_t         psk[GOODIX_TLS_PSK_SIZE];
    size_t          psk_len;
    char            peer_identity[128];
} goodix_tls;

/* Loads a key from a file holding it as hex digits; separators and newlines
 * are ignored. The key is never logged, and the read buffer is wiped. */
int goodix_tls_load_psk(const char *path, uint8_t *psk, size_t psk_size);

int  goodix_tls_init(goodix_tls *tls, goodix_tls_role role,
                     const uint8_t *psk, size_t psk_len);
void goodix_tls_free(goodix_tls *tls);

/* Hands bytes received from the peer to OpenSSL. */
int goodix_tls_feed(goodix_tls *tls, const uint8_t *data, size_t len);

/* Collects bytes OpenSSL wants sent. Returns how many, 0 if none. */
int goodix_tls_take(goodix_tls *tls, uint8_t *buf, size_t buf_size);

/* Drives the handshake as far as it can go:
 *   1  done
 *   0  needs more input
 *  <0  failed */
int goodix_tls_handshake_step(goodix_tls *tls);

/* Decrypts what has been fed in since the last call. Returns the number of
 * plaintext bytes produced, 0 if more ciphertext is needed, <0 on error. */
int goodix_tls_read(goodix_tls *tls, uint8_t *buf, size_t buf_size);

const char *goodix_tls_cipher(const goodix_tls *tls);

/* Human-readable names for what shows up on the wire. */
const char *goodix_tls_record_name(uint8_t type);
const char *goodix_tls_alert_name(uint8_t description);
const char *goodix_tls_last_error(void);

#endif /* GOODIX_TLS_H */
