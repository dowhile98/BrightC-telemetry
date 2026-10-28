/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cwt_tm16pt_codec.h"

#include <errno.h>

#include "modbus_decode.h"

/** Lowest and highest plausible temperature reported by the transmitter. */
#define CWT_TM16PT_TEMP_MIN_C (-200.0f)
#define CWT_TM16PT_TEMP_MAX_C (850.0f)

int cwt_tm16pt_parse_scaled_temperatures(const uint16_t *regs, size_t reg_count,
                                         temperature_bank_16ch_t *out_bank)
{
    if (regs == NULL || out_bank == NULL || reg_count < CWT_TM16PT_SCALED_REG_COUNT) {
        return -EINVAL;
    }

    out_bank->valid_channels_mask = 0U;
    out_bank->open_probe_mask = 0U;
    out_bank->is_communication_valid = true;

    for (size_t i = 0; i < CWT_TM16PT_CHANNEL_COUNT; i++) {
        uint16_t raw_word = regs[i];

        /* Check for open probe / disconnection sentinel (0x7FFF = 32767 = 3276.7 C) */
        if (raw_word == CWT_TM16PT_PROBE_OPEN_CODE || (int16_t)raw_word == -32768) {
            out_bank->open_probe_mask |= (1U << i);
            out_bank->temperatures_c[i] = 0.0f;
            continue;
        }

        float temp_c = 0.0f;
        int err = modbus_decode_int16_scaled(raw_word, 0.1f, &temp_c);
        if (err == 0 && temp_c >= CWT_TM16PT_TEMP_MIN_C && temp_c <= CWT_TM16PT_TEMP_MAX_C) {
            out_bank->temperatures_c[i] = temp_c;
            out_bank->valid_channels_mask |= (1U << i);
        } else {
            out_bank->open_probe_mask |= (1U << i);
            out_bank->temperatures_c[i] = 0.0f;
        }
    }

    return 0;
}

int cwt_tm16pt_parse_float_temperatures(const uint16_t *regs, size_t reg_count,
                                        temperature_bank_16ch_t *out_bank)
{
    if (regs == NULL || out_bank == NULL || reg_count < CWT_TM16PT_FLOAT_REG_COUNT) {
        return -EINVAL;
    }

    out_bank->valid_channels_mask = 0U;
    out_bank->open_probe_mask = 0U;
    out_bank->is_communication_valid = true;

    for (size_t i = 0; i < CWT_TM16PT_CHANNEL_COUNT; i++) {
        uint16_t msw = regs[i * 2];
        uint16_t lsw = regs[i * 2 + 1];

        float temp_c = 0.0f;
        int err = modbus_decode_float32(msw, lsw, MODBUS_ENDIANNESS_BE, &temp_c);
        if (err == 0 && temp_c >= CWT_TM16PT_TEMP_MIN_C && temp_c <= CWT_TM16PT_TEMP_MAX_C) {
            out_bank->temperatures_c[i] = temp_c;
            out_bank->valid_channels_mask |= (1U << i);
        } else {
            out_bank->open_probe_mask |= (1U << i);
            out_bank->temperatures_c[i] = 0.0f;
        }
    }

    return 0;
}

int cwt_tm16pt_build_config_register(uint8_t slave_id, uint32_t baudrate, uint8_t parity,
                                     uint16_t *out_reg)
{
    if (slave_id == 0U || out_reg == NULL) {
        return -EINVAL;
    }

    uint8_t baud_bits = 0U;
    switch (baudrate) {
    case 9600:
        baud_bits = 0U;
        break;
    case 1200:
        baud_bits = 1U;
        break;
    case 2400:
        baud_bits = 2U;
        break;
    case 4800:
        baud_bits = 3U;
        break;
    case 14400:
        baud_bits = 5U;
        break;
    case 19200:
        baud_bits = 6U;
        break;
    case 38400:
        baud_bits = 7U;
        break;
    default:
        return -EINVAL;
    }

    uint8_t parity_bits = 0U;
    switch (parity) {
    case 0U: /* None */
        parity_bits = 0U;
        break;
    case 1U: /* Even */
        parity_bits = 1U;
        break;
    case 2U: /* Odd */
        parity_bits = 2U;
        break;
    default:
        return -EINVAL;
    }

    uint8_t low_byte = (uint8_t) ((baud_bits & 0x07U) | ((parity_bits & 0x03U) << 3));
    *out_reg = (uint16_t) (((uint16_t) slave_id << 8) | (uint16_t) low_byte);

    return 0;
}

int cwt_tm16pt_parse_config_register(uint16_t reg_val, uint8_t *out_slave_id,
                                     uint32_t *out_baudrate, uint8_t *out_parity)
{
    if (out_slave_id == NULL || out_baudrate == NULL || out_parity == NULL) {
        return -EINVAL;
    }

    *out_slave_id = (uint8_t) (reg_val >> 8);
    uint8_t low_byte = (uint8_t) (reg_val & 0xFFU);

    uint8_t baud_bits = low_byte & 0x07U;
    switch (baud_bits) {
    case 0:
        *out_baudrate = 9600U;
        break;
    case 1:
        *out_baudrate = 1200U;
        break;
    case 2:
        *out_baudrate = 2400U;
        break;
    case 3:
        *out_baudrate = 4800U;
        break;
    case 4:
        *out_baudrate = 9600U;
        break;
    case 5:
        *out_baudrate = 14400U;
        break;
    case 6:
        *out_baudrate = 19200U;
        break;
    case 7:
        *out_baudrate = 38400U;
        break;
    default:
        *out_baudrate = 9600U;
        break;
    }

    uint8_t parity_bits = (uint8_t) ((low_byte >> 3) & 0x03U);
    if (parity_bits == 0U) {
        *out_parity = 0U; /* None */
    } else if (parity_bits == 1U) {
        *out_parity = 1U; /* Even */
    } else {
        *out_parity = 2U; /* Odd */
    }

    return 0;
}
