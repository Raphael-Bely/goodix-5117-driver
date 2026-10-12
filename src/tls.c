/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/err.h>

#include "goodix/tls.h"

/* The sensor announces no PSK identity hint, so any short string will do. */
#define GOODIX_TLS_IDENTITY "goodix"

static unsigned int psk_server_cb(SSL *ssl, const char *identity,
                                  unsigned char *psk, unsigned int max_psk_len)
{
    goodix_tls *tls = SSL_get_app_data(ssl);

    if (tls == NULL || tls->psk_len > max_psk_len)
        return 0;

    if (identity)
        snprintf(tls->peer_identity, sizeof(tls->peer_identity), "%s", identity);

    memcpy(psk, tls->psk, tls->psk_len);
    return (unsigned int)tls->psk_len;
}

static unsigned int psk_client_cb(SSL *ssl, const char *hint, char *identity,
                                  unsigned int max_identity_len,
                                  unsigned char *psk, unsigned int max_psk_len)
{
    goodix_tls *tls = SSL_get_app_data(ssl);

    (void)hint;

    if (tls == NULL || tls->psk_len > max_psk_len)
        return 0;
    if (strlen(GOODIX_TLS_IDENTITY) + 1 > max_identity_len)
        return 0;

    snprintf(identity, max_identity_len, "%s", GOODIX_TLS_IDENTITY);
    memcpy(psk, tls->psk, tls->psk_len);
    return (unsigned int)tls->psk_len;
}

int goodix_tls_load_psk(const char *path, uint8_t *psk, size_t psk_size)
{
    char   text[256];
    FILE  *file;
    size_t read_len;
    size_t digits = 0;
    size_t out    = 0;
    int    high   = -1;
    int    rc     = GOODIX_OK;

    file = fopen(path, "r");
    if (file == NULL)
        return GOODIX_ERR_IO;

    read_len = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[read_len] = '\0';

    for (size_t i = 0; i < read_len; i++)
        if (isxdigit((unsigned char)text[i]))
            digits++;

    if (digits != psk_size * 2) {
        rc = GOODIX_ERR_BUFFER;
        goto done;
    }

    for (size_t i = 0; i < read_len; i++) {
        int c = (unsigned char)text[i];
        int value;

        if (!isxdigit(c))
            continue;

        value = (c <= '9') ? c - '0' : (tolower(c) - 'a' + 10);

        if (high < 0) {
            high = value;
        } else {
            psk[out++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }

done:
    /* Do not leave key material in the read buffer. */
    explicit_bzero(text, sizeof(text));
    return rc;
}

int goodix_tls_init(goodix_tls *tls, goodix_tls_role role,
                    const uint8_t *psk, size_t psk_len)
{
    const SSL_METHOD *method;

    if (psk_len > sizeof(tls->psk))
        return GOODIX_ERR_BUFFER;

    memset(tls, 0, sizeof(*tls));
    tls->role    = role;
    tls->psk_len = psk_len;
    memcpy(tls->psk, psk, psk_len);

    method = (role == GOODIX_TLS_SERVER) ? TLS_server_method()
                                         : TLS_client_method();
    tls->ctx = SSL_CTX_new(method);
    if (tls->ctx == NULL)
        return GOODIX_ERR_TLS;

    /* The sensor speaks TLS 1.2 and nothing else. */
    SSL_CTX_set_min_proto_version(tls->ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(tls->ctx, TLS1_2_VERSION);

    /* Plain PSK offers no forward secrecy, which the distribution's default
     * security level rejects. */
    SSL_CTX_set_security_level(tls->ctx, 0);

    if (SSL_CTX_set_cipher_list(tls->ctx, GOODIX_TLS_CIPHER) != 1)
        goto fail;

    if (role == GOODIX_TLS_SERVER)
        SSL_CTX_set_psk_server_callback(tls->ctx, psk_server_cb);
    else
        SSL_CTX_set_psk_client_callback(tls->ctx, psk_client_cb);

    tls->ssl = SSL_new(tls->ctx);
    if (tls->ssl == NULL)
        goto fail;

    SSL_set_app_data(tls->ssl, tls);

    tls->rbio = BIO_new(BIO_s_mem());
    tls->wbio = BIO_new(BIO_s_mem());
    if (tls->rbio == NULL || tls->wbio == NULL)
        goto fail;

    /* Without this an empty buffer reads as end-of-file and the handshake
     * aborts; -1 makes it report "nothing yet, ask me again". */
    BIO_set_mem_eof_return(tls->rbio, -1);
    BIO_set_mem_eof_return(tls->wbio, -1);

    /* SSL_set_bio takes ownership of both BIOs. */
    SSL_set_bio(tls->ssl, tls->rbio, tls->wbio);

    if (role == GOODIX_TLS_SERVER)
        SSL_set_accept_state(tls->ssl);
    else
        SSL_set_connect_state(tls->ssl);

    return GOODIX_OK;

fail:
    goodix_tls_free(tls);
    return GOODIX_ERR_TLS;
}

void goodix_tls_free(goodix_tls *tls)
{
    if (tls->ssl) {
        SSL_free(tls->ssl);   /* frees both BIOs too */
        tls->ssl  = NULL;
        tls->rbio = NULL;
        tls->wbio = NULL;
    } else {
        if (tls->rbio)
            BIO_free(tls->rbio);
        if (tls->wbio)
            BIO_free(tls->wbio);
        tls->rbio = NULL;
        tls->wbio = NULL;
    }

    if (tls->ctx) {
        SSL_CTX_free(tls->ctx);
        tls->ctx = NULL;
    }

    explicit_bzero(tls->psk, sizeof(tls->psk));
    tls->psk_len = 0;
}

int goodix_tls_feed(goodix_tls *tls, const uint8_t *data, size_t len)
{
    if (len == 0)
        return GOODIX_OK;

    return BIO_write(tls->rbio, data, (int)len) == (int)len ? GOODIX_OK
                                                            : GOODIX_ERR_TLS;
}

int goodix_tls_take(goodix_tls *tls, uint8_t *buf, size_t buf_size)
{
    int pending = BIO_pending(tls->wbio);

    if (pending <= 0)
        return 0;
    if ((size_t)pending > buf_size)
        pending = (int)buf_size;

    return BIO_read(tls->wbio, buf, pending);
}

int goodix_tls_handshake_step(goodix_tls *tls)
{
    int rc  = SSL_do_handshake(tls->ssl);
    int err;

    if (rc == 1)
        return 1;

    err = SSL_get_error(tls->ssl, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return 0;

    return GOODIX_ERR_TLS;
}

int goodix_tls_read(goodix_tls *tls, uint8_t *buf, size_t buf_size)
{
    int rc = SSL_read(tls->ssl, buf, (int)buf_size);
    int err;

    if (rc > 0)
        return rc;

    err = SSL_get_error(tls->ssl, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return 0;

    return GOODIX_ERR_TLS;
}

const char *goodix_tls_cipher(const goodix_tls *tls)
{
    const char *name = SSL_get_cipher_name(tls->ssl);

    return name ? name : "(none)";
}

const char *goodix_tls_record_name(uint8_t type)
{
    switch (type) {
    case 0x14: return "change cipher spec";
    case 0x15: return "alert";
    case 0x16: return "handshake";
    case 0x17: return "application data";
    default:   return "unknown";
    }
}

const char *goodix_tls_alert_name(uint8_t description)
{
    switch (description) {
    case 0:  return "close_notify";
    case 10: return "unexpected_message";
    case 20: return "bad_record_mac";
    case 40: return "handshake_failure";
    case 47: return "illegal_parameter";
    case 51: return "decrypt_error";
    case 80: return "internal_error";
    default: return "other";
    }
}

const char *goodix_tls_last_error(void)
{
    static char   msg[256];
    unsigned long code = ERR_get_error();

    if (code == 0)
        return "no OpenSSL error queued";

    ERR_error_string_n(code, msg, sizeof(msg));
    return msg;
}
