/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Negotiates the encrypted channel and reports what was agreed. */

#include <stdio.h>

#include "goodix/session.h"

int main(void)
{
    goodix_device  dev;
    goodix_session session;
    int            rc;

    rc = goodix_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open: %s\n", goodix_strerror(rc));
        return 1;
    }

    rc = goodix_session_open(&session, &dev, NULL, 1);
    if (rc < 0) {
        fprintf(stderr, "\nhandshake failed: %s\n", goodix_strerror(rc));
        fprintf(stderr, "openssl: %s\n", goodix_tls_last_error());
        fprintf(stderr, "\nA bad_record_mac alert above means the sensor holds "
                        "a key of its own.\nSee SPEC.md section 6 on recovering "
                        "it from Windows.\n");
        goodix_session_close(&session);
        goodix_close(&dev);
        return 1;
    }

    printf("\nhandshake complete\n");
    printf("  cipher:       %s\n", goodix_tls_cipher(&session.tls));
    printf("  psk identity: %s\n", session.tls.peer_identity);

    goodix_session_close(&session);
    goodix_close(&dev);
    return 0;
}
