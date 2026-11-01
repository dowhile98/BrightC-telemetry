/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register Codec for the MBSL32DI 32-Channel Digital Input Module.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#include "domain/services/mbsl32di_codec.h"

#include <errno.h>

bool mbsl32di_get_channel(const mbsl32di_reading_t *reading, uint8_t channel_1_to_32)
{
    if (reading == NULL || !reading->is_valid) {
        return false;
    }

    if (channel_1_to_32 < MBSL32DI_CHANNEL_MIN || channel_1_to_32 > MBSL32DI_CHANNEL_MAX) {
        return false;
    }

    return (reading->raw_inputs & (1UL << (channel_1_to_32 - 1U))) != 0U;
}

int mbsl32di_parse_inputs(const uint16_t *regs, size_t reg_count, mbsl32di_reading_t *out_reading)
{
    if (regs == NULL || out_reading == NULL || reg_count < MBSL32DI_INPUTS_REG_COUNT) {
        return -EINVAL;
    }

    out_reading->raw_inputs = ((uint32_t) regs[0]) | (((uint32_t) regs[1]) << 16);
    out_reading->is_valid = true;

    return 0;
}

int mbsl32di_parse_config(const uint16_t *regs, size_t reg_count, mbsl32di_config_t *out_cfg)
{
    if (regs == NULL || out_cfg == NULL || reg_count < MBSL32DI_CONFIG_WRITE_REG_COUNT) {
        return -EINVAL;
    }

    /* Reg 0x0002: Slave Address (1..255) */
    if (regs[0] < 1U || regs[0] > 255U) {
        return -EINVAL;
    }

    /* Reg 0x0003: Baud Rate Code */
    uint32_t baudrate;
    switch (regs[1]) {
    case MBSL32DI_BAUD_CODE_4800:
        baudrate = 4800U;
        break;
    case MBSL32DI_BAUD_CODE_9600:
        baudrate = 9600U;
        break;
    case MBSL32DI_BAUD_CODE_19200:
        baudrate = 19200U;
        break;
    case MBSL32DI_BAUD_CODE_38400:
        baudrate = 38400U;
        break;
    case MBSL32DI_BAUD_CODE_57600:
        baudrate = 57600U;
        break;
    case MBSL32DI_BAUD_CODE_115200:
        baudrate = 115200U;
        break;
    default:
        return -EINVAL;
    }

    /* Reg 0x0004: Parity Code (1=None, 2=Odd, 3=Even) */
    if (regs[2] < MBSL32DI_PARITY_NONE || regs[2] > MBSL32DI_PARITY_EVEN) {
        return -EINVAL;
    }

    out_cfg->slave_addr = (uint8_t) regs[0];
    out_cfg->baudrate = baudrate;
    out_cfg->parity = (uint8_t) regs[2];
    out_cfg->timeout_s = regs[3];

    /* Reg 0x0006: Firmware / Software Version */
    if (reg_count >= MBSL32DI_CONFIG_REG_COUNT) {
        out_cfg->version = regs[4];
    } else {
        out_cfg->version = 0U;
    }

    return 0;
}

int mbsl32di_build_config_registers(uint8_t slave_addr, uint32_t baudrate, uint8_t parity,
                                    uint16_t timeout_s, uint16_t *out_regs, size_t out_count)
{
    if (out_regs == NULL || out_count < MBSL32DI_CONFIG_WRITE_REG_COUNT) {
        return -EINVAL;
    }

    if (slave_addr == 0U) {
        return -EINVAL;
    }

    uint16_t baud_code;
    switch (baudrate) {
    case 4800U:
        baud_code = MBSL32DI_BAUD_CODE_4800;
        break;
    case 9600U:
        baud_code = MBSL32DI_BAUD_CODE_9600;
        break;
    case 19200U:
        baud_code = MBSL32DI_BAUD_CODE_19200;
        break;
    case 38400U:
        baud_code = MBSL32DI_BAUD_CODE_38400;
        break;
    case 57600U:
        baud_code = MBSL32DI_BAUD_CODE_57600;
        break;
    case 115200U:
        baud_code = MBSL32DI_BAUD_CODE_115200;
        break;
    default:
        return -EINVAL;
    }

    if (parity < MBSL32DI_PARITY_NONE || parity > MBSL32DI_PARITY_EVEN) {
        return -EINVAL;
    }

    out_regs[0] = (uint16_t) slave_addr;
    out_regs[1] = baud_code;
    out_regs[2] = (uint16_t) parity;
    out_regs[3] = timeout_s;

    return 0;
}
