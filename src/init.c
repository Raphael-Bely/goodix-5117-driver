/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <string.h>

#include "goodix/init.h"

static const uint8_t payload_none[2]      = { 0x00, 0x00 };
static const uint8_t payload_nop[4]       = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t payload_enable[2]    = { 0x01, 0x00 };
static const uint8_t payload_reset[2]     = { 0x01, 0x14 };
static const uint8_t payload_idle[2]      = { 0x14, 0x00 };
static const uint8_t payload_powerdown[2] = { 0x64, 0x00 };

/* GOODIX_PSK_FLAGS then a zero length, both little-endian. */
static const uint8_t payload_psk_read[8] = { 0x03, 0x00, 0x02, 0xbb,
                                             0x00, 0x00, 0x00, 0x00 };

static const uint16_t otp_registers[4] = { 0x0220, 0x0236, 0x0238, 0x023a };

/* PRESET_PSK_READ reply: status(1) + flags(4) + length(4) + key. */
#define PSK_REPLY_HEADER 9

static void report_step(goodix_progress_fn progress, void *user,
                        int step, const char *label)
{
    if (progress)
        progress(step, GOODIX_INIT_STEPS, label, user);
}

static int read_firmware(goodix_device *dev, uint8_t *buf, size_t size,
                         goodix_init_report *report)
{
    goodix_message reply;
    size_t         len;
    int            rc;

    rc = goodix_command_reply(dev, GOODIX_CMD_FIRMWARE_VERSION,
                              payload_none, sizeof(payload_none),
                              buf, size, &reply);
    if (rc < 0)
        return rc;

    len = reply.payload_len;
    if (len >= sizeof(report->firmware))
        len = sizeof(report->firmware) - 1;

    memcpy(report->firmware, reply.payload, len);
    report->firmware[len] = '\0';

    return GOODIX_OK;
}

static int read_psk(goodix_device *dev, uint8_t *buf, size_t size,
                    goodix_init_report *report)
{
    goodix_message reply;
    uint32_t       flags;
    uint32_t       length;
    int            rc;

    rc = goodix_command_reply(dev, GOODIX_CMD_PRESET_PSK_READ,
                              payload_psk_read, sizeof(payload_psk_read),
                              buf, size, &reply);
    if (rc < 0)
        return rc;

    if (reply.payload_len < PSK_REPLY_HEADER)
        return GOODIX_ERR_SHORT;

    flags  = goodix_le32(reply.payload + 1);
    length = goodix_le32(reply.payload + 5);

    if (flags != GOODIX_PSK_FLAGS)
        return GOODIX_ERR_PROTOCOL;
    if (length != GOODIX_PSK_SIZE ||
        reply.payload_len < PSK_REPLY_HEADER + GOODIX_PSK_SIZE)
        return GOODIX_ERR_SHORT;

    memcpy(report->psk, reply.payload + PSK_REPLY_HEADER, GOODIX_PSK_SIZE);
    report->psk_is_factory =
        memcmp(report->psk, goodix_factory_psk, GOODIX_PSK_SIZE) == 0;

    return GOODIX_OK;
}

static int apply_otp(goodix_device *dev, uint8_t *buf, size_t size,
                     goodix_init_report *report)
{
    goodix_message reply;
    int            rc;

    rc = goodix_command_reply(dev, GOODIX_CMD_READ_OTP,
                              payload_none, sizeof(payload_none),
                              buf, size, &reply);
    if (rc < 0)
        return rc;

    if (reply.payload_len < 50)
        return GOODIX_ERR_SHORT;

    /* Extract before sending anything else: the register writes reuse `buf`,
     * which is where reply.payload points. */
    report->calibration[0] = (uint16_t)((reply.payload[46] << 4) | 8);
    report->calibration[1] = reply.payload[47];
    report->calibration[2] = reply.payload[48];
    report->calibration[3] = reply.payload[49];

    for (int i = 0; i < 4; i++) {
        const uint8_t write[5] = {
            0x00,
            (uint8_t)(otp_registers[i] & 0xff),
            (uint8_t)(otp_registers[i] >> 8),
            (uint8_t)(report->calibration[i] & 0xff),
            (uint8_t)(report->calibration[i] >> 8),
        };

        rc = goodix_command(dev, GOODIX_CMD_WRITE_SENSOR_REGISTER,
                            write, sizeof(write));
        if (rc < 0)
            return rc;
    }

    return GOODIX_OK;
}

int goodix_initialise(goodix_device *dev, goodix_progress_fn progress,
                      void *user, goodix_init_report *report)
{
    goodix_init_report  scratch;
    goodix_message      reply;
    uint8_t             buf[GOODIX_MAX_RESPONSE];
    int                 rc;

    if (report == NULL)
        report = &scratch;
    memset(report, 0, sizeof(*report));

    report_step(progress, user, 1, "nop");
    rc = goodix_send(dev, GOODIX_CMD_NOP, payload_nop, sizeof(payload_nop),
                     GOODIX_CHECKSUM_NULL);
    if (rc < 0)
        return rc;

    report_step(progress, user, 2, "enable chip");
    rc = goodix_command(dev, GOODIX_CMD_ENABLE_CHIP,
                        payload_enable, sizeof(payload_enable));
    if (rc < 0)
        return rc;

    report_step(progress, user, 3, "nop");
    rc = goodix_send(dev, GOODIX_CMD_NOP, payload_nop, sizeof(payload_nop),
                     GOODIX_CHECKSUM_NULL);
    if (rc < 0)
        return rc;

    report_step(progress, user, 4, "firmware version");
    rc = read_firmware(dev, buf, sizeof(buf), report);
    if (rc < 0)
        return rc;

    report_step(progress, user, 5, "read psk");
    rc = read_psk(dev, buf, sizeof(buf), report);
    if (rc < 0)
        return rc;

    report_step(progress, user, 6, "reset sensor");
    rc = goodix_command_reply(dev, GOODIX_CMD_RESET,
                              payload_reset, sizeof(payload_reset),
                              buf, sizeof(buf), &reply);
    if (rc < 0)
        return rc;

    report_step(progress, user, 7, "mcu idle mode");
    rc = goodix_command(dev, GOODIX_CMD_MCU_SWITCH_TO_IDLE_MODE,
                        payload_idle, sizeof(payload_idle));
    if (rc < 0)
        return rc;

    report_step(progress, user, 8, "read otp, apply calibration");
    rc = apply_otp(dev, buf, sizeof(buf), report);
    if (rc < 0)
        return rc;

    report_step(progress, user, 9, "upload mcu config");
    rc = goodix_command_reply(dev, GOODIX_CMD_UPLOAD_CONFIG_MCU,
                              goodix_mcu_config, GOODIX_MCU_CONFIG_SIZE,
                              buf, sizeof(buf), &reply);
    if (rc < 0)
        return rc;

    report_step(progress, user, 10, "powerdown scan frequency");
    return goodix_command_reply(dev, GOODIX_CMD_SET_POWERDOWN_SCAN_FREQ,
                                payload_powerdown, sizeof(payload_powerdown),
                                buf, sizeof(buf), &reply);
}
