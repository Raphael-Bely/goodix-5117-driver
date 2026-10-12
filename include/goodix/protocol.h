/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GOODIX_PROTOCOL_H
#define GOODIX_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "goodix/device.h"

#define GOODIX_FLAG_MESSAGE 0xa0
#define GOODIX_FLAG_TLS     0xb0

#define GOODIX_CMD_NOP                     0x00
#define GOODIX_CMD_MCU_GET_IMAGE           0x20
#define GOODIX_CMD_MCU_SWITCH_TO_FDT_DOWN  0x32
#define GOODIX_CMD_MCU_SWITCH_TO_FDT_UP    0x34
#define GOODIX_CMD_MCU_SWITCH_TO_FDT_MODE  0x36
#define GOODIX_CMD_NAV_0                   0x50
#define GOODIX_CMD_MCU_SWITCH_TO_IDLE_MODE 0x70
#define GOODIX_CMD_WRITE_SENSOR_REGISTER   0x80
#define GOODIX_CMD_READ_SENSOR_REGISTER    0x82
#define GOODIX_CMD_UPLOAD_CONFIG_MCU       0x90
#define GOODIX_CMD_SET_POWERDOWN_SCAN_FREQ 0x94
#define GOODIX_CMD_ENABLE_CHIP             0x96
#define GOODIX_CMD_RESET                   0xa2
#define GOODIX_CMD_QUERY_MCU_STATE         0xae
#define GOODIX_CMD_READ_OTP                0xa6
#define GOODIX_CMD_FIRMWARE_VERSION        0xa8
#define GOODIX_CMD_ACK                     0xb0
#define GOODIX_CMD_REQUEST_TLS_CONNECTION  0xd0
#define GOODIX_CMD_TLS_ESTABLISHED         0xd4
/* Bootloader-era command, never exercised on this chip family. Read-only. */
#define GOODIX_CMD_READ_FIRMWARE           0xf2
#define GOODIX_CMD_PRESET_PSK_READ         0xe4

/* Sent instead of a computed checksum by some commands. */
#define GOODIX_NULL_CHECKSUM 0x88

#define GOODIX_PACK_HEADER_SIZE     4
#define GOODIX_PROTOCOL_HEADER_SIZE 3

#define GOODIX_MAX_COMMAND  512
#define GOODIX_MAX_RESPONSE 32768

/* Sensor geometry, see SPEC.md section 8. 88 pixels per row, 80 rows. */
#define GOODIX_IMAGE_WIDTH  88
#define GOODIX_IMAGE_HEIGHT 80
#define GOODIX_IMAGE_PIXELS (GOODIX_IMAGE_WIDTH * GOODIX_IMAGE_HEIGHT)

/* Only the first 64 columns carry data; the remaining 24 always read zero.
 * Measured on the target, see SPEC.md section 8. */
#define GOODIX_IMAGE_USEFUL_WIDTH 64
#define GOODIX_IMAGE_USEFUL_PIXELS \
    (GOODIX_IMAGE_USEFUL_WIDTH * GOODIX_IMAGE_HEIGHT)

/* Six bytes carry four 12-bit pixels, with a header and a trailer. */
#define GOODIX_IMAGE_HEAD     8
#define GOODIX_IMAGE_TAIL     5
#define GOODIX_IMAGE_RAW_SIZE \
    (GOODIX_IMAGE_HEAD + GOODIX_IMAGE_PIXELS / 4 * 6 + GOODIX_IMAGE_TAIL)

#define GOODIX_PSK_SIZE        32
#define GOODIX_PSK_FLAGS       0xbb020003u
#define GOODIX_MCU_CONFIG_SIZE 256

extern const uint8_t goodix_factory_psk[GOODIX_PSK_SIZE];
extern const uint8_t goodix_mcu_config[GOODIX_MCU_CONFIG_SIZE];

/* Most commands carry a computed checksum; NOP sends a fixed 0x88 instead. */
typedef enum {
    GOODIX_CHECKSUM_COMPUTED,
    GOODIX_CHECKSUM_NULL,
} goodix_checksum_mode;

/* Decoded protocol-layer message. `payload` points into the caller's buffer
 * and stays valid only until that buffer is reused. */
typedef struct {
    uint8_t        cmd;
    const uint8_t *payload;
    uint16_t       payload_len;
} goodix_message;

/* The Goodix protocol is little-endian throughout. */
static inline uint16_t goodix_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t goodix_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t goodix_checksum(const uint8_t *data, size_t len);

int goodix_encode_protocol(uint8_t cmd, const uint8_t *payload,
                           uint16_t payload_len, goodix_checksum_mode mode,
                           uint8_t *out, size_t out_size, size_t *out_len);

int goodix_encode_pack(uint8_t flags, const uint8_t *payload,
                       uint16_t payload_len, uint8_t *out,
                       size_t out_size, size_t *out_len);

/* How many bytes the frame starting at `buf` occupies, or 0 while the header
 * has not fully arrived. Lets a reader know when to stop. */
size_t goodix_pack_size(const uint8_t *buf, size_t len);

/* Splits a complete frame into its envelope type and contents. Pure: no
 * device, no I/O. `payload` points into `buf`. */
int goodix_decode_pack(const uint8_t *buf, size_t len, uint8_t *flags,
                       const uint8_t **payload, uint16_t *payload_len);

/* Splits the contents of a command envelope into command and payload. */
int goodix_decode_message(const uint8_t *body, uint16_t body_len,
                          goodix_message *msg);

/* Reads one complete frame and returns its envelope as-is. Use this when the
 * envelope may carry TLS data (flags 0xb0) rather than a command. */
int goodix_receive_pack(goodix_device *dev, uint8_t *buf, size_t buf_size,
                        uint8_t *flags, const uint8_t **payload,
                        uint16_t *payload_len);

/* Same, but requires a command envelope and decodes the message inside it. */
int goodix_receive_message(goodix_device *dev, uint8_t *buf, size_t buf_size,
                           goodix_message *msg);

/* Three levels, each built on the previous one:
 *   send          - write the frame, wait for nothing (NOP only)
 *   command       - send, then read and validate the ACK
 *   command_reply - send, validate the ACK, then read the answer */
int goodix_send(goodix_device *dev, uint8_t cmd, const uint8_t *payload,
                uint16_t payload_len, goodix_checksum_mode mode);

int goodix_command(goodix_device *dev, uint8_t cmd, const uint8_t *payload,
                   uint16_t payload_len);

int goodix_command_reply(goodix_device *dev, uint8_t cmd,
                         const uint8_t *payload, uint16_t payload_len,
                         uint8_t *buf, size_t buf_size, goodix_message *reply);

#endif /* GOODIX_PROTOCOL_H */
