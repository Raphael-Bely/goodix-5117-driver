/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Runs the TLS handshake between our server and a stand-in for the sensor,
 * entirely in memory. No USB, no sensor, nothing to recover from.
 *
 * Pass --mismatch to give the two sides different keys, which is what a
 * re-provisioned sensor would look like. */

#include <stdio.h>
#include <string.h>

#include "goodix/tls.h"

#define MAX_ROUNDS 8
#define BUF_SIZE   4096

/* Advances one endpoint, then hands whatever it produced to the other. The
 * order matters: a failing endpoint still emits an alert worth forwarding. */
static int pump(goodix_tls *from, goodix_tls *to, const char *label)
{
    uint8_t buf[BUF_SIZE];
    int     status = goodix_tls_handshake_step(from);
    int     n      = goodix_tls_take(from, buf, sizeof(buf));

    if (n > 0) {
        printf("  %-6s -> %4d bytes  [%s]", label, n, goodix_tls_record_name(buf[0]));

        /* An alert sent before the cipher change is still readable. */
        if (buf[0] == 0x15 && n >= 7)
            printf(" level %u, %s (%u)", buf[5], goodix_tls_alert_name(buf[6]), buf[6]);

        printf("\n");
        goodix_tls_feed(to, buf, (size_t)n);
    }

    return status;
}

static const char *state_name(int status)
{
    if (status < 0)  return "failed";
    if (status == 1) return "done";
    return "waiting";
}

int main(int argc, char **argv)
{
    uint8_t server_psk[GOODIX_TLS_PSK_SIZE] = { 0 };
    uint8_t client_psk[GOODIX_TLS_PSK_SIZE] = { 0 };

    goodix_tls server;
    goodix_tls client;
    int        client_status = 0;
    int        server_status = 0;
    int        status        = 1;
    int        rc;

    if (argc == 2 && strcmp(argv[1], "--mismatch") == 0) {
        memset(client_psk, 0x42, sizeof(client_psk));
        printf("keys differ on purpose\n\n");
    } else {
        printf("both sides hold 32 zero bytes\n\n");
    }

    rc = goodix_tls_init(&server, GOODIX_TLS_SERVER,
                         server_psk, sizeof(server_psk));
    if (rc < 0) {
        fprintf(stderr, "server init: %s\n", goodix_tls_last_error());
        return 1;
    }

    rc = goodix_tls_init(&client, GOODIX_TLS_CLIENT,
                         client_psk, sizeof(client_psk));
    if (rc < 0) {
        fprintf(stderr, "client init: %s\n", goodix_tls_last_error());
        goodix_tls_free(&server);
        return 1;
    }

    for (int round = 1; round <= MAX_ROUNDS; round++) {
        printf("round %d\n", round);

        client_status = pump(&client, &server, "sensor");
        server_status = pump(&server, &client, "host");

        if (client_status < 0 || server_status < 0) {
            printf("\nhandshake failed\n");
            printf("  sensor side: %s\n", state_name(client_status));
            printf("  host side:   %s\n", state_name(server_status));
            printf("  openssl:     %s\n", goodix_tls_last_error());
            goto out;
        }

        if (client_status == 1 && server_status == 1) {
            printf("\nhandshake complete\n");
            printf("  cipher:       %s\n", goodix_tls_cipher(&server));
            printf("  psk identity: %s\n", server.peer_identity);
            status = 0;
            goto out;
        }
    }

    printf("\nno conclusion after %d rounds\n", MAX_ROUNDS);

out:
    goodix_tls_free(&client);
    goodix_tls_free(&server);
    return status;
}
