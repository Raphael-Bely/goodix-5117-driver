/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <string.h>

#include "goodix/capture.h"

static const uint8_t payload_none[2]  = { 0x00, 0x00 };
static const uint8_t payload_nop[4]   = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t payload_mcu[1]   = { 0x55 };
static const uint8_t payload_image[2] = { 0x01, 0x00 };

/* Opaque mode payloads, SPEC.md section 7. */
static const uint8_t fdt_1[14] = { 0x0d, 0x01, 0xb2, 0xb2, 0xc2, 0xc2, 0xa7,
                                   0xa7, 0xb6, 0xb6, 0xa6, 0xa6, 0xb6, 0xb6 };
static const uint8_t fdt_2[14] = { 0x0d, 0x01, 0x80, 0xb0, 0x80, 0xc0, 0x80,
                                   0xa4, 0x80, 0xb4, 0x80, 0xa3, 0x80, 0xb4 };
static const uint8_t fdt_3[14] = { 0x0d, 0x01, 0x80, 0xb1, 0x80, 0xc1, 0x80,
                                   0xa6, 0x80, 0xb6, 0x80, 0xa5, 0x80, 0xb6 };
static const uint8_t fdt_4[14] = { 0x0c, 0x01, 0x80, 0xb1, 0x80, 0xc1, 0x80,
                                   0xa6, 0x80, 0xb6, 0x80, 0xa5, 0x80, 0xb6 };
static const uint8_t fdt_5[14] = { 0x0c, 0x01, 0x80, 0xb2, 0x80, 0xc2, 0x80,
                                   0xa7, 0x80, 0xb6, 0x80, 0xa6, 0x80, 0xb6 };
static const uint8_t fdt_6[14] = { 0x0e, 0x01, 0x80, 0x90, 0x80, 0x9a, 0x80,
                                   0x7b, 0x80, 0x95, 0x80, 0x8c, 0x80, 0xa3 };
static const uint8_t fdt_7[14] = { 0x0c, 0x01, 0x80, 0xa7, 0x80, 0xb9, 0x80,
                                   0xa3, 0x80, 0xb5, 0x80, 0xa4, 0x80, 0xb6 };

static const uint16_t otp_registers[4] = { 0x0220, 0x0236, 0x0238, 0x023a };

static int fdt(goodix_session *session, uint8_t cmd, const uint8_t *mode)
{
    goodix_message reply;

    return goodix_command_reply(session->dev, cmd, mode, 14, session->frame,
                                sizeof(session->frame), &reply);
}

static int read_register(goodix_session *session, uint16_t address,
                         uint8_t length)
{
    const uint8_t request[5] = {
        0x00, (uint8_t)(address & 0xff), (uint8_t)(address >> 8), length, 0x00,
    };
    goodix_message reply;

    return goodix_command_reply(session->dev, GOODIX_CMD_READ_SENSOR_REGISTER,
                                request, sizeof(request), session->frame,
                                sizeof(session->frame), &reply);
}

static int query_state(goodix_session *session)
{
    goodix_message reply;

    return goodix_command_reply(session->dev, GOODIX_CMD_QUERY_MCU_STATE,
                                payload_mcu, sizeof(payload_mcu),
                                session->frame, sizeof(session->frame),
                                &reply);
}

static int nav(goodix_session *session)
{
    goodix_message reply;

    return goodix_command_reply(session->dev, GOODIX_CMD_NAV_0, payload_none,
                                sizeof(payload_none), session->frame,
                                sizeof(session->frame), &reply);
}

static int nop(goodix_session *session)
{
    return goodix_send(session->dev, GOODIX_CMD_NOP, payload_nop,
                       sizeof(payload_nop), GOODIX_CHECKSUM_NULL);
}

int goodix_capture_prepare(goodix_session *session)
{
    int rc;

    rc = read_register(session, 0x0000, 4);     /* chip id */
    if (rc < 0)
        return rc;

    rc = nop(session);
    if (rc < 0)
        return rc;

    rc = query_state(session);
    if (rc < 0)
        return rc;

    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_MODE, fdt_1);
    if (rc < 0)
        return rc;

    rc = nav(session);
    if (rc < 0)
        return rc;

    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_MODE, fdt_2);
    if (rc < 0)
        return rc;

    rc = read_register(session, 0x0082, 2);
    if (rc < 0)
        return rc;

    /* This chip's own calibration, read from its OTP at startup. The
     * reference tool hardcodes its author's values instead. */
    for (int i = 0; i < 4; i++) {
        uint16_t value = session->report.calibration[i];
        const uint8_t request[5] = {
            0x00,
            (uint8_t)(otp_registers[i] & 0xff),
            (uint8_t)(otp_registers[i] >> 8),
            (uint8_t)(value & 0xff),
            (uint8_t)(value >> 8),
        };

        rc = goodix_command(session->dev, GOODIX_CMD_WRITE_SENSOR_REGISTER,
                            request, sizeof(request));
        if (rc < 0)
            return rc;
    }

    return GOODIX_OK;
}

int goodix_capture_frame(goodix_session *session, uint8_t *raw)
{
    int rc = goodix_command(session->dev, GOODIX_CMD_MCU_GET_IMAGE,
                            payload_image, sizeof(payload_image));
    if (rc < 0)
        return rc;

    return goodix_session_read(session, raw, GOODIX_IMAGE_RAW_SIZE);
}

int goodix_capture_wait_finger(goodix_session *session, unsigned timeout_ms)
{
    unsigned saved = session->dev->timeout_ms;
    int      rc;

    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_MODE, fdt_3);
    if (rc < 0)
        return rc;

    session->dev->timeout_ms = timeout_ms;

    /* Both fdt_down calls block; the reference tool prints its prompt after
     * the first one, which hides that. */
    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_DOWN, fdt_4);
    if (rc < 0)
        goto done;

    rc = nop(session);
    if (rc < 0)
        goto done;

    rc = query_state(session);
    if (rc < 0)
        goto done;

    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_DOWN, fdt_5);

done:
    session->dev->timeout_ms = saved;
    return rc;
}

int goodix_capture_wait_lift(goodix_session *session, unsigned timeout_ms)
{
    unsigned saved = session->dev->timeout_ms;
    int      rc;

    session->dev->timeout_ms = timeout_ms;
    rc = fdt(session, GOODIX_CMD_MCU_SWITCH_TO_FDT_UP, fdt_6);
    session->dev->timeout_ms = saved;

    if (rc < 0)
        return rc;

    rc = nav(session);
    if (rc < 0)
        return rc;

    /* Required after a capture, or the sensor stops answering. */
    return goodix_command(session->dev, GOODIX_CMD_MCU_SWITCH_TO_FDT_DOWN,
                          fdt_7, 14);
}
