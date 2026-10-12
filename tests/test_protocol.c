/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "goodix/protocol.h"
#include "test.h"

/* The firmware version request, byte for byte. Every field of both layers is
 * visible here, so a change to either shows up as a diff. */
static const uint8_t message_a8[]  = { 0xa8, 0x03, 0x00, 0x00, 0x00, 0xff };
static const uint8_t pack_a8[]     = { 0xa0, 0x06, 0x00, 0xa6,
                                       0xa8, 0x03, 0x00, 0x00, 0x00, 0xff };

/* Enable chip: a different command, to catch a hardcoded checksum. */
static const uint8_t message_96[]  = { 0x96, 0x03, 0x00, 0x01, 0x00, 0x10 };

static void checksum_wraps_at_eight_bits (void)
{
    const uint8_t data[] = { 0xff, 0x02 };

    TEST_EQ (goodix_checksum (data, 2), 0x01);
    TEST_EQ (goodix_checksum (data, 0), 0x00);
}

static void little_endian_helpers (void)
{
    const uint8_t bytes[] = { 0x03, 0x00, 0x02, 0xbb };

    TEST_EQ (goodix_le16 (bytes), 0x0003);
    TEST_EQ (goodix_le32 (bytes), 0xbb020003u);
}

static void encodes_the_message_layer (void)
{
    const uint8_t payload[2] = { 0x00, 0x00 };
    uint8_t out[64];
    size_t  len = 0;

    TEST_EQ (goodix_encode_protocol (GOODIX_CMD_FIRMWARE_VERSION, payload, 2,
                                     GOODIX_CHECKSUM_COMPUTED, out,
                                     sizeof (out), &len), GOODIX_OK);
    TEST_EQ (len, sizeof (message_a8));
    TEST_MEM (out, message_a8, sizeof (message_a8));
}

static void message_checksum_follows_the_command (void)
{
    const uint8_t payload[2] = { 0x01, 0x00 };
    uint8_t out[64];
    size_t  len = 0;

    TEST_EQ (goodix_encode_protocol (GOODIX_CMD_ENABLE_CHIP, payload, 2,
                                     GOODIX_CHECKSUM_COMPUTED, out,
                                     sizeof (out), &len), GOODIX_OK);
    TEST_MEM (out, message_96, sizeof (message_96));
}

static void null_checksum_is_a_constant (void)
{
    const uint8_t payload[4] = { 0, 0, 0, 0 };
    uint8_t out[64];
    size_t  len = 0;

    TEST_EQ (goodix_encode_protocol (GOODIX_CMD_NOP, payload, 4,
                                     GOODIX_CHECKSUM_NULL, out,
                                     sizeof (out), &len), GOODIX_OK);
    TEST_EQ (out[len - 1], GOODIX_NULL_CHECKSUM);
}

static void encodes_the_envelope_and_pads (void)
{
    uint8_t out[128];
    size_t  len = 0;

    TEST_EQ (goodix_encode_pack (GOODIX_FLAG_MESSAGE, message_a8,
                                 sizeof (message_a8), out, sizeof (out), &len),
             GOODIX_OK);
    TEST_EQ (len, GOODIX_MAX_PACKET);
    TEST_MEM (out, pack_a8, sizeof (pack_a8));

    for (size_t i = sizeof (pack_a8); i < len; i++)
        TEST_EQ (out[i], 0);
}

static void padding_rounds_up_to_the_packet_size (void)
{
    uint8_t payload[256];
    uint8_t out[512];
    size_t  len = 0;

    memset (payload, 0x5a, sizeof (payload));

    /* 4 header + 256 payload = 260, which rounds to 320. */
    TEST_EQ (goodix_encode_pack (GOODIX_FLAG_MESSAGE, payload,
                                 sizeof (payload), out, sizeof (out), &len),
             GOODIX_OK);
    TEST_EQ (len, 320);
}

static void refuses_a_buffer_that_is_too_small (void)
{
    const uint8_t payload[2] = { 0, 0 };
    uint8_t small[4];
    size_t  len = 0;

    TEST_EQ (goodix_encode_protocol (GOODIX_CMD_FIRMWARE_VERSION, payload, 2,
                                     GOODIX_CHECKSUM_COMPUTED, small,
                                     sizeof (small), &len),
             GOODIX_ERR_BUFFER);
    TEST_EQ (goodix_encode_pack (GOODIX_FLAG_MESSAGE, payload, 2, small,
                                 sizeof (small), &len),
             GOODIX_ERR_BUFFER);
}

static void pack_size_needs_the_header_first (void)
{
    TEST_EQ (goodix_pack_size (pack_a8, 0), 0);
    TEST_EQ (goodix_pack_size (pack_a8, 3), 0);
    TEST_EQ (goodix_pack_size (pack_a8, 4), 10);
    TEST_EQ (goodix_pack_size (pack_a8, 64), 10);
}

static void decodes_what_it_encoded (void)
{
    const uint8_t *payload = NULL;
    uint16_t       payload_len = 0;
    uint8_t        flags = 0;

    TEST_EQ (goodix_decode_pack (pack_a8, sizeof (pack_a8), &flags, &payload,
                                 &payload_len), GOODIX_OK);
    TEST_EQ (flags, GOODIX_FLAG_MESSAGE);
    TEST_EQ (payload_len, sizeof (message_a8));
    TEST_MEM (payload, message_a8, sizeof (message_a8));
}

static void rejects_a_truncated_frame (void)
{
    const uint8_t *payload = NULL;
    uint16_t       payload_len = 0;
    uint8_t        flags = 0;

    TEST_EQ (goodix_decode_pack (pack_a8, sizeof (pack_a8) - 1, &flags,
                                 &payload, &payload_len), GOODIX_ERR_SHORT);
}

static void rejects_a_corrupt_envelope_checksum (void)
{
    uint8_t        broken[sizeof (pack_a8)];
    const uint8_t *payload = NULL;
    uint16_t       payload_len = 0;
    uint8_t        flags = 0;

    memcpy (broken, pack_a8, sizeof (broken));
    broken[3] ^= 0xff;

    TEST_EQ (goodix_decode_pack (broken, sizeof (broken), &flags, &payload,
                                 &payload_len), GOODIX_ERR_CHECKSUM);
}

static void decodes_a_message (void)
{
    goodix_message msg;

    TEST_EQ (goodix_decode_message (message_a8, sizeof (message_a8), &msg),
             GOODIX_OK);
    TEST_EQ (msg.cmd, GOODIX_CMD_FIRMWARE_VERSION);
    TEST_EQ (msg.payload_len, 2);
}

static void rejects_a_corrupt_message_checksum (void)
{
    uint8_t        broken[sizeof (message_a8)];
    goodix_message msg;

    memcpy (broken, message_a8, sizeof (broken));
    broken[sizeof (broken) - 1] = 0x01;   /* neither correct nor the null one */

    TEST_EQ (goodix_decode_message (broken, sizeof (broken), &msg),
             GOODIX_ERR_CHECKSUM);
}

static void accepts_the_null_checksum_on_the_way_in (void)
{
    uint8_t        nulled[sizeof (message_a8)];
    goodix_message msg;

    memcpy (nulled, message_a8, sizeof (nulled));
    nulled[sizeof (nulled) - 1] = GOODIX_NULL_CHECKSUM;

    TEST_EQ (goodix_decode_message (nulled, sizeof (nulled), &msg), GOODIX_OK);
}

int main (void)
{
    TEST_RUN (checksum_wraps_at_eight_bits);
    TEST_RUN (little_endian_helpers);
    TEST_RUN (encodes_the_message_layer);
    TEST_RUN (message_checksum_follows_the_command);
    TEST_RUN (null_checksum_is_a_constant);
    TEST_RUN (encodes_the_envelope_and_pads);
    TEST_RUN (padding_rounds_up_to_the_packet_size);
    TEST_RUN (refuses_a_buffer_that_is_too_small);
    TEST_RUN (pack_size_needs_the_header_first);
    TEST_RUN (decodes_what_it_encoded);
    TEST_RUN (rejects_a_truncated_frame);
    TEST_RUN (rejects_a_corrupt_envelope_checksum);
    TEST_RUN (decodes_a_message);
    TEST_RUN (rejects_a_corrupt_message_checksum);
    TEST_RUN (accepts_the_null_checksum_on_the_way_in);

    TEST_SUMMARY ("protocol");
}
