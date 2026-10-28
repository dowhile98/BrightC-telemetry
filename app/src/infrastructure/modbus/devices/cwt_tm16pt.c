/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cwt_tm16pt.h"
#include <errno.h>
#include <codec_modbus/modbus_decode.h>

int cwt_tm16pt_parse_scaled_temperatures(const uint16_t *regs, size_t reg_count, temperature_bank_16ch_t *out_bank)
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
        if (err == 0 && temp_c >= -200.0f && temp_c <= 850.0f) {
            out_bank->temperatures_c[i] = temp_c;
            out_bank->valid_channels_mask |= (1U << i);
        } else {
            out_bank->open_probe_mask |= (1U << i);
            out_bank->temperatures_c[i] = 0.0f;
        }
    }

    return 0;
}

int cwt_tm16pt_parse_float_temperatures(const uint16_t *regs, size_t reg_count, temperature_bank_16ch_t *out_bank)
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
        if (err == 0 && temp_c >= -200.0f && temp_c <= 850.0f) {
            out_bank->temperatures_c[i] = temp_c;
            out_bank->valid_channels_mask |= (1U << i);
        } else {
            out_bank->open_probe_mask |= (1U << i);
            out_bank->temperatures_c[i] = 0.0f;
        }
    }

    return 0;
}

int cwt_tm16pt_build_config_register(uint8_t slave_id, uint32_t baudrate, uint8_t parity, uint16_t *out_reg)
{
    if (slave_id == 0 || out_reg == NULL) {
        return -EINVAL;
    }

    uint8_t baud_bits = 0;
    switch (baudrate) {
    case 9600:   baud_bits = 0; break;
    case 1200:   baud_bits = 1; break;
    case 2400:   baud_bits = 2; break;
    case 4800:   baud_bits = 3; break;
    case 14400:  baud_bits = 5; break;
    case 19200:  baud_bits = 6; break;
    case 38400:  baud_bits = 7; break;
    default:
        return -EINVAL;
    }

    uint8_t parity_bits = 0;
    switch (parity) {
    case 0: /* None */ parity_bits = 0; break;
    case 1: /* Even */ parity_bits = 1; break;
    case 2: /* Odd  */ parity_bits = 2; break;
    default:
        return -EINVAL;
    }

    uint8_t low_byte = (baud_bits & 0x07U) | ((parity_bits & 0x03U) << 3);
    *out_reg = ((uint16_t)slave_id << 8) | (uint16_t)low_byte;

    return 0;
}

int cwt_tm16pt_parse_config_register(uint16_t reg_val, uint8_t *out_slave_id, uint32_t *out_baudrate, uint8_t *out_parity)
{
    if (out_slave_id == NULL || out_baudrate == NULL || out_parity == NULL) {
        return -EINVAL;
    }

    *out_slave_id = (uint8_t)(reg_val >> 8);
    uint8_t low_byte = (uint8_t)(reg_val & 0xFFU);

    uint8_t baud_bits = low_byte & 0x07U;
    switch (baud_bits) {
    case 0: *out_baudrate = 9600; break;
    case 1: *out_baudrate = 1200; break;
    case 2: *out_baudrate = 2400; break;
    case 3: *out_baudrate = 4800; break;
    case 4: *out_baudrate = 9600; break;
    case 5: *out_baudrate = 14400; break;
    case 6: *out_baudrate = 19200; break;
    case 7: *out_baudrate = 38400; break;
    default: *out_baudrate = 9600; break;
    }

    uint8_t parity_bits = (low_byte >> 3) & 0x03U;
    if (parity_bits == 0) {
        *out_parity = 0; /* None */
    } else if (parity_bits == 1) {
        *out_parity = 1; /* Even */
    } else {
        *out_parity = 2; /* Odd */
    }

    return 0;
}

int cwt_tm16pt_read_temperatures(nmbs_z_client_t *client, uint8_t slave_addr, 
                                temperature_bank_16ch_t *out_bank, k_timeout_t lock_timeout)
{
    if (client == NULL || out_bank == NULL || slave_addr == 0) {
        return -EINVAL;
    }

    uint16_t raw_regs[CWT_TM16PT_SCALED_REG_COUNT];

    /* Thread-safe read using Zephyr nanoMODBUS client */
    int err = nmbs_z_client_read_holding_registers_addr(client, slave_addr, 
                                                        CWT_TM16PT_REG_TEMP_SCALED_BASE,
                                                        CWT_TM16PT_SCALED_REG_COUNT, 
                                                        raw_regs, lock_timeout);
    if (err != 0) {
        out_bank->is_communication_valid = false;
        out_bank->valid_channels_mask = 0U;
        return err;
    }

    return cwt_tm16pt_parse_scaled_temperatures(raw_regs, CWT_TM16PT_SCALED_REG_COUNT, out_bank);
}

int cwt_tm16pt_write_bus_config(nmbs_z_client_t *client, uint8_t current_addr, 
                                uint8_t new_addr, uint32_t baudrate, uint8_t parity, 
                                k_timeout_t lock_timeout)
{
    if (client == NULL || current_addr == 0) {
        return -EINVAL;
    }

    uint16_t packed_val = 0;
    int err = cwt_tm16pt_build_config_register(new_addr, baudrate, parity, &packed_val);
    if (err) {
        return err;
    }

    /* Thread-safe single register write using Zephyr nanoMODBUS client */
    return nmbs_z_client_write_single_register_addr(client, current_addr, 
                                                    CWT_TM16PT_REG_CONFIG, packed_val, 
                                                    lock_timeout);
}
