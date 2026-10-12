/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <string.h>

#include "goodix/protocol.h"

uint8_t goodix_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++)
        sum += data[i];

    return sum;
}

int goodix_encode_protocol(uint8_t cmd, const uint8_t *payload,
                           uint16_t payload_len, goodix_checksum_mode mode,
                           uint8_t *out, size_t out_size, size_t *out_len)
{
    size_t len = GOODIX_PROTOCOL_HEADER_SIZE + payload_len + 1;
    /* The length field counts the trailing checksum byte. */
    uint16_t field = (uint16_t)(payload_len + 1);

    if (out_size < len)
        return GOODIX_ERR_BUFFER;

    out[0] = cmd;
    out[1] = (uint8_t)(field & 0xff);
    out[2] = (uint8_t)(field >> 8);
    memcpy(out + GOODIX_PROTOCOL_HEADER_SIZE, payload, payload_len);

    out[len - 1] = (mode == GOODIX_CHECKSUM_NULL)
                       ? GOODIX_NULL_CHECKSUM
                       : (uint8_t)(0xaa - goodix_checksum(out, len - 1));

    *out_len = len;
    return GOODIX_OK;
}

int goodix_encode_pack(uint8_t flags, const uint8_t *payload,
                       uint16_t payload_len, uint8_t *out,
                       size_t out_size, size_t *out_len)
{
    size_t len    = GOODIX_PACK_HEADER_SIZE + payload_len;
    size_t padded = (len + GOODIX_MAX_PACKET - 1) / GOODIX_MAX_PACKET
                    * GOODIX_MAX_PACKET;

    if (out_size < padded)
        return GOODIX_ERR_BUFFER;

    out[0] = flags;
    out[1] = (uint8_t)(payload_len & 0xff);
    out[2] = (uint8_t)(payload_len >> 8);
    /* Pack checksum only covers the three header bytes. */
    out[3] = goodix_checksum(out, 3);
    memcpy(out + GOODIX_PACK_HEADER_SIZE, payload, payload_len);
    memset(out + len, 0, padded - len);

    *out_len = padded;
    return GOODIX_OK;
}

size_t goodix_pack_size(const uint8_t *buf, size_t len)
{
    if (len < GOODIX_PACK_HEADER_SIZE)
        return 0;

    return GOODIX_PACK_HEADER_SIZE + goodix_le16(buf + 1);
}

int goodix_decode_pack(const uint8_t *buf, size_t len, uint8_t *flags,
                       const uint8_t **payload, uint16_t *payload_len)
{
    size_t expected = goodix_pack_size(buf, len);

    if (expected == 0 || len < expected)
        return GOODIX_ERR_SHORT;

    /* The envelope checksum covers the three header bytes and nothing else. */
    if (buf[3] != goodix_checksum(buf, 3))
        return GOODIX_ERR_CHECKSUM;

    *flags       = buf[0];
    *payload     = buf + GOODIX_PACK_HEADER_SIZE;
    *payload_len = (uint16_t)(expected - GOODIX_PACK_HEADER_SIZE);

    return GOODIX_OK;
}

int goodix_decode_message(const uint8_t *body, uint16_t body_len,
                          goodix_message *msg)
{
    size_t   checksum_offset;
    uint16_t field;
    uint16_t payload_len;
    uint8_t  expected;

    if (body_len < GOODIX_PROTOCOL_HEADER_SIZE + 1)
        return GOODIX_ERR_SHORT;

    /* The length field counts the trailing checksum byte. */
    field = goodix_le16(body + 1);

    if (field == 0 || GOODIX_PROTOCOL_HEADER_SIZE + field > body_len)
        return GOODIX_ERR_SHORT;

    payload_len     = (uint16_t)(field - 1);
    checksum_offset = GOODIX_PROTOCOL_HEADER_SIZE + payload_len;
    expected        = (uint8_t)(0xaa - goodix_checksum(body, checksum_offset));

    if (body[checksum_offset] != expected &&
        body[checksum_offset] != GOODIX_NULL_CHECKSUM)
        return GOODIX_ERR_CHECKSUM;

    msg->cmd         = body[0];
    msg->payload     = body + GOODIX_PROTOCOL_HEADER_SIZE;
    msg->payload_len = payload_len;

    return GOODIX_OK;
}

int goodix_receive_pack(goodix_device *dev, uint8_t *buf, size_t buf_size,
                        uint8_t *flags, const uint8_t **payload,
                        uint16_t *payload_len)
{
    size_t total = 0;
    size_t expected = 0;
    int    rc;

    /* A frame can span several packets, and its size is only known once the
     * header has arrived. */
    do {
        size_t received = 0;

        if (total + GOODIX_MAX_PACKET > buf_size)
            return GOODIX_ERR_BUFFER;

        rc = goodix_read(dev, buf + total, GOODIX_MAX_PACKET, &received);
        if (rc < 0)
            return rc;
        if (received == 0)
            return GOODIX_ERR_IO;

        total   += received;
        expected = goodix_pack_size(buf, total);
    } while (expected == 0 || total < expected);

    return goodix_decode_pack(buf, total, flags, payload, payload_len);
}

int goodix_receive_message(goodix_device *dev, uint8_t *buf, size_t buf_size,
                           goodix_message *msg)
{
    const uint8_t *body;
    uint16_t       body_len;
    uint8_t        flags;
    int            rc;

    rc = goodix_receive_pack(dev, buf, buf_size, &flags, &body, &body_len);
    if (rc < 0)
        return rc;

    if (flags != GOODIX_FLAG_MESSAGE)
        return GOODIX_ERR_PROTOCOL;

    return goodix_decode_message(body, body_len, msg);
}

static int check_ack(const goodix_message *ack, uint8_t cmd)
{
    if (ack->cmd != GOODIX_CMD_ACK || ack->payload_len < 2)
        return GOODIX_ERR_PROTOCOL;
    if (ack->payload[0] != cmd)
        return GOODIX_ERR_PROTOCOL;
    /* Bit 0 is set on a healthy ACK; bit 1 flags a missing MCU config. */
    if ((ack->payload[1] & 0x01) == 0)
        return GOODIX_ERR_PROTOCOL;

    return GOODIX_OK;
}

int goodix_send(goodix_device *dev, uint8_t cmd, const uint8_t *payload,
                uint16_t payload_len, goodix_checksum_mode mode)
{
    uint8_t message[GOODIX_MAX_COMMAND];
    uint8_t frame[GOODIX_MAX_COMMAND];
    size_t  message_len;
    size_t  frame_len;
    int     rc;

    rc = goodix_encode_protocol(cmd, payload, payload_len, mode,
                                message, sizeof(message), &message_len);
    if (rc < 0)
        return rc;

    rc = goodix_encode_pack(GOODIX_FLAG_MESSAGE, message, (uint16_t)message_len,
                            frame, sizeof(frame), &frame_len);
    if (rc < 0)
        return rc;

    return goodix_write(dev, frame, frame_len);
}

int goodix_command(goodix_device *dev, uint8_t cmd, const uint8_t *payload,
                   uint16_t payload_len)
{
    uint8_t        buf[GOODIX_MAX_PACKET];
    goodix_message ack;
    int            rc;

    rc = goodix_send(dev, cmd, payload, payload_len, GOODIX_CHECKSUM_COMPUTED);
    if (rc < 0)
        return rc;

    rc = goodix_receive_message(dev, buf, sizeof(buf), &ack);
    if (rc < 0)
        return rc;

    return check_ack(&ack, cmd);
}

int goodix_command_reply(goodix_device *dev, uint8_t cmd,
                         const uint8_t *payload, uint16_t payload_len,
                         uint8_t *buf, size_t buf_size, goodix_message *reply)
{
    goodix_message ack;
    int            rc;

    rc = goodix_send(dev, cmd, payload, payload_len, GOODIX_CHECKSUM_COMPUTED);
    if (rc < 0)
        return rc;

    /* The ACK lands in the same buffer and is overwritten by the answer, so it
     * must be validated before the second read. */
    rc = goodix_receive_message(dev, buf, buf_size, &ack);
    if (rc < 0)
        return rc;

    rc = check_ack(&ack, cmd);
    if (rc < 0)
        return rc;

    return goodix_receive_message(dev, buf, buf_size, reply);
}
