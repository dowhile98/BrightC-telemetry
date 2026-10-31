/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register Codec for the JSY-MK-339 Three-Phase AC Electrical Energy Meter.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#include "jsy_mk339_codec.h"

#include <errno.h>
#include <string.h>

/** Helper to decode Big-Endian 32-bit unsigned integer from two Modbus 16-bit words. */
static inline uint32_t decode_u32_be(uint16_t msw, uint16_t lsw)
{
    return ((uint32_t)msw << 16) | (uint32_t)lsw;
}

int jsy_mk339_parse_electrical_parameters(const uint16_t *regs, size_t reg_count,
                                          energy_reading_t *out_reading)
{
    if (regs == NULL || out_reading == NULL || reg_count < JSY_MK339_ELECTRICAL_BASIC_REG_COUNT) {
        return -EINVAL;
    }

    memset(out_reading, 0, sizeof(*out_reading));
    out_reading->is_valid = true;

    /* Voltages: 0x0048..0x004A (val = DATA / 100, unit V) */
    out_reading->voltage_v[ENERGY_PHASE_A] = (float)regs[0] * 0.01f;
    out_reading->voltage_v[ENERGY_PHASE_B] = (float)regs[1] * 0.01f;
    out_reading->voltage_v[ENERGY_PHASE_C] = (float)regs[2] * 0.01f;

    /* Currents: 0x004B..0x004D (val = DATA / 100, unit A) */
    out_reading->current_a[ENERGY_PHASE_A] = (float)regs[3] * 0.01f;
    out_reading->current_a[ENERGY_PHASE_B] = (float)regs[4] * 0.01f;
    out_reading->current_a[ENERGY_PHASE_C] = (float)regs[5] * 0.01f;

    /* Leakage current: 0x004E (val = DATA / 10000, unit A) */
    out_reading->leakage_current_a = (float)regs[6] * 0.0001f;

    /* Active powers: 0x004F..0x0052 (val = DATA / 100, unit kW) */
    out_reading->active_power_kw[ENERGY_PHASE_A] = (float)regs[7] * 0.01f;
    out_reading->active_power_kw[ENERGY_PHASE_B] = (float)regs[8] * 0.01f;
    out_reading->active_power_kw[ENERGY_PHASE_C] = (float)regs[9] * 0.01f;
    out_reading->total_active_power_kw = (float)regs[10] * 0.01f;

    /* Reactive powers: 0x0053..0x0056 (val = DATA / 100, unit kVar) */
    out_reading->reactive_power_kvar[ENERGY_PHASE_A] = (float)regs[11] * 0.01f;
    out_reading->reactive_power_kvar[ENERGY_PHASE_B] = (float)regs[12] * 0.01f;
    out_reading->reactive_power_kvar[ENERGY_PHASE_C] = (float)regs[13] * 0.01f;
    out_reading->total_reactive_power_kvar = (float)regs[14] * 0.01f;

    /* Apparent powers: 0x0057..0x005A (val = DATA / 100, unit kVA) */
    out_reading->apparent_power_kva[ENERGY_PHASE_A] = (float)regs[15] * 0.01f;
    out_reading->apparent_power_kva[ENERGY_PHASE_B] = (float)regs[16] * 0.01f;
    out_reading->apparent_power_kva[ENERGY_PHASE_C] = (float)regs[17] * 0.01f;
    out_reading->total_apparent_power_kva = (float)regs[18] * 0.01f;

    /* Power factors: 0x005B..0x005E (val = DATA / 1000) */
    out_reading->power_factor[ENERGY_PHASE_A] = (float)regs[19] * 0.001f;
    out_reading->power_factor[ENERGY_PHASE_B] = (float)regs[20] * 0.001f;
    out_reading->power_factor[ENERGY_PHASE_C] = (float)regs[21] * 0.001f;
    out_reading->total_power_factor = (float)regs[22] * 0.001f;

    /* Accumulated active energies: 0x005F..0x0066 (U32 BE, val = DATA / 100, unit kWh) */
    out_reading->active_energy_kwh[ENERGY_PHASE_A] = (double)decode_u32_be(regs[23], regs[24]) * 0.01;
    out_reading->active_energy_kwh[ENERGY_PHASE_B] = (double)decode_u32_be(regs[25], regs[26]) * 0.01;
    out_reading->active_energy_kwh[ENERGY_PHASE_C] = (double)decode_u32_be(regs[27], regs[28]) * 0.01;
    out_reading->total_active_energy_kwh = (double)decode_u32_be(regs[29], regs[30]) * 0.01;

    /* Accumulated reactive energies: 0x0067..0x006E (if present, U32 BE, val = DATA / 100, kvarh) */
    if (reg_count >= 39U) {
        out_reading->reactive_energy_kvarh[ENERGY_PHASE_A] = (double)decode_u32_be(regs[31], regs[32]) * 0.01;
        out_reading->reactive_energy_kvarh[ENERGY_PHASE_B] = (double)decode_u32_be(regs[33], regs[34]) * 0.01;
        out_reading->reactive_energy_kvarh[ENERGY_PHASE_C] = (double)decode_u32_be(regs[35], regs[36]) * 0.01;
        out_reading->total_reactive_energy_kvarh = (double)decode_u32_be(regs[37], regs[38]) * 0.01;
    }

    /* Cumulative apparent energies: 0x006F..0x0076 (if present, U32 BE, val = DATA / 100, kVAh) */
    if (reg_count >= 47U) {
        out_reading->apparent_energy_kvah[ENERGY_PHASE_A] = (double)decode_u32_be(regs[39], regs[40]) * 0.01;
        out_reading->apparent_energy_kvah[ENERGY_PHASE_B] = (double)decode_u32_be(regs[41], regs[42]) * 0.01;
        out_reading->apparent_energy_kvah[ENERGY_PHASE_C] = (double)decode_u32_be(regs[43], regs[44]) * 0.01;
        out_reading->total_apparent_energy_kvah = (double)decode_u32_be(regs[45], regs[46]) * 0.01;
    }

    /* Frequency: 0x0077 (or 0x0076, val = DATA / 100, unit Hz) */
    if (reg_count >= 48U) {
        out_reading->frequency_hz = (float)regs[47] * 0.01f;
    }

    return 0;
}

int jsy_mk339_build_config_register(uint8_t slave_addr, uint32_t baudrate, uint16_t *out_reg)
{
    if (slave_addr == 0U || out_reg == NULL) {
        return -EINVAL;
    }

    uint8_t baud_code = 0U;
    switch (baudrate) {
    case 4800:
        baud_code = JSY_MK339_BAUD_CODE_4800;
        break;
    case 9600:
        baud_code = JSY_MK339_BAUD_CODE_9600;
        break;
    case 19200:
        baud_code = JSY_MK339_BAUD_CODE_19200;
        break;
    default:
        return -EINVAL;
    }

    *out_reg = (uint16_t)(((uint16_t)slave_addr << 8) | (uint16_t)baud_code);
    return 0;
}

int jsy_mk339_parse_config_register(uint16_t reg_val, uint8_t *out_slave_addr,
                                    uint32_t *out_baudrate)
{
    if (out_slave_addr == NULL || out_baudrate == NULL) {
        return -EINVAL;
    }

    uint8_t baud_code = (uint8_t)(reg_val & 0xFFU);
    switch (baud_code) {
    case JSY_MK339_BAUD_CODE_4800:
        *out_baudrate = 4800U;
        break;
    case JSY_MK339_BAUD_CODE_9600:
        *out_baudrate = 9600U;
        break;
    case JSY_MK339_BAUD_CODE_19200:
        *out_baudrate = 19200U;
        break;
    default:
        return -EINVAL;
    }

    *out_slave_addr = (uint8_t)(reg_val >> 8);
    return 0;
}
