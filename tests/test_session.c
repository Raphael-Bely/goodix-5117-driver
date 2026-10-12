/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <unistd.h>

#include "goodix/session.h"
#include "test.h"

#define TMP_KEY "/tmp/goodix-test.psk"

static const uint8_t expected[GOODIX_TLS_PSK_SIZE] = {
    0x3f, 0x8a, 0x1c, 0x09, 0xd4, 0xe7, 0xb2, 0x66,
    0x0a, 0x1f, 0x55, 0xc3, 0xe9, 0x8d, 0x47, 0x0b,
    0x2c, 0x6a, 0x81, 0xfe, 0x4d, 0x93, 0x05, 0x7b,
    0xac, 0x12, 0xe6, 0xf8, 0x0d, 0x35, 0x49, 0xa7,
};

static void write_key_file (const char *text)
{
    FILE *handle = fopen (TMP_KEY, "w");

    if (handle == NULL)
        return;

    fputs (text, handle);
    fclose (handle);
}

static void reads_lowercase_hex (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("3f8a1c09d4e7b2660a1f55c3e98d470b"
                    "2c6a81fe4d93057bac12e6f80d3549a7");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)), GOODIX_OK);
    TEST_MEM (psk, expected, sizeof (expected));
    unlink (TMP_KEY);
}

/* The Windows tool prints the key in upper case. */
static void reads_uppercase_hex (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("3F8A1C09D4E7B2660A1F55C3E98D470B"
                    "2C6A81FE4D93057BAC12E6F80D3549A7");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)), GOODIX_OK);
    TEST_MEM (psk, expected, sizeof (expected));
    unlink (TMP_KEY);
}

/* Copying and pasting leaves newlines and spaces behind. */
static void ignores_whitespace (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("  3f8a1c09 d4e7b266 0a1f55c3 e98d470b\n"
                    "  2c6a81fe 4d93057b ac12e6f8 0d3549a7\n\n");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)), GOODIX_OK);
    TEST_MEM (psk, expected, sizeof (expected));
    unlink (TMP_KEY);
}

static void refuses_a_short_key (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("3f8a1c09d4e7b266");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)),
             GOODIX_ERR_BUFFER);
    unlink (TMP_KEY);
}

static void refuses_a_long_key (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("3f8a1c09d4e7b2660a1f55c3e98d470b"
                    "2c6a81fe4d93057bac12e6f80d3549a7"
                    "ff");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)),
             GOODIX_ERR_BUFFER);
    unlink (TMP_KEY);
}

/* A missing file is not an error the caller should stop on: it simply means
 * this sensor still holds the factory key. */
static void a_missing_file_is_reported_separately (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    TEST_EQ (goodix_tls_load_psk ("/tmp/goodix-no-such-key", psk,
                                  sizeof (psk)), GOODIX_ERR_IO);
}

static void an_empty_file_is_not_a_key (void)
{
    uint8_t psk[GOODIX_TLS_PSK_SIZE];

    write_key_file ("");

    TEST_EQ (goodix_tls_load_psk (TMP_KEY, psk, sizeof (psk)),
             GOODIX_ERR_BUFFER);
    unlink (TMP_KEY);
}

int main (void)
{
    TEST_RUN (reads_lowercase_hex);
    TEST_RUN (reads_uppercase_hex);
    TEST_RUN (ignores_whitespace);
    TEST_RUN (refuses_a_short_key);
    TEST_RUN (refuses_a_long_key);
    TEST_RUN (a_missing_file_is_reported_separately);
    TEST_RUN (an_empty_file_is_not_a_key);

    TEST_SUMMARY ("session");
}
