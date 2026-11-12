/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_modbus_tcp_server.c
 * @brief Infrastructure Adapter implementation for nanoMODBUS TCP Server callbacks.
 *
 * Adheres strictly to Clean Architecture: delegates all domain queries and commands
 * exclusively through app_facade_*() APIs.
 */

#include "adapter_modbus_tcp_server.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "application/services/app_facade.h"

LOG_MODULE_REGISTER(adapter_modbus_tcp, LOG_LEVEL_INF);

int adapter_modbus_tcp_server_init(adapter_modbus_tcp_server_t *self,
                                   const modbus_tcp_server_config_t *config)
{
    if (self == NULL || config == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->word_order = config->word_order;

    nmbs_callbacks_create(&self->callbacks);
    self->callbacks.read_coils = adapter_modbus_tcp_on_read_coils;
    self->callbacks.write_single_coil = adapter_modbus_tcp_on_write_single_coil;
    self->callbacks.write_multiple_coils = adapter_modbus_tcp_on_write_multiple_coils;
    self->callbacks.read_discrete_inputs = adapter_modbus_tcp_on_read_discrete_inputs;
    self->callbacks.read_input_registers = adapter_modbus_tcp_on_read_input_registers;
    self->callbacks.read_holding_registers = adapter_modbus_tcp_on_read_holding_registers;
    self->callbacks.write_single_register = adapter_modbus_tcp_on_write_single_register;
    self->callbacks.write_multiple_registers = adapter_modbus_tcp_on_write_multiple_registers;
    self->callbacks.arg = self;

    self->supervisory_opcode = 0U;
    self->supervisory_arg = 0U;
    self->supervisory_status = 0U;
    self->supervisory_last_opcode = 0U;
    self->supervisory_last_errno = 0U;

    self->is_initialized = true;
    return 0;
}

const nmbs_callbacks *adapter_modbus_tcp_server_get_callbacks(const adapter_modbus_tcp_server_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return NULL;
    }
    return &self->callbacks;
}

/* ========================================================================= */
/* 1. DISCRETE INPUTS (Function Code 0x02)                                   */
/* ========================================================================= */

nmbs_error adapter_modbus_tcp_on_read_discrete_inputs(uint16_t address, uint16_t quantity,
                                                      nmbs_bitfield inputs_out,
                                                      uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (quantity == 0U) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
    }

    /* Section 1: Digital Inputs 10001..10056 (PDU 0x0000 .. 0x0037) */
    if (address < MODBUS_DI_COUNT_DISCRETE_INPUTS &&
        (address + quantity) <= MODBUS_DI_COUNT_DISCRETE_INPUTS) {
        di_snapshot_msg_t snap;
        int err = app_facade_di_get_snapshot(&snap);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t ch = address + i;
            bool active = (ch < snap.channel_count) ? snap.is_active[ch] : false;
            nmbs_bitfield_write(inputs_out, i, active);
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 2: Active Alarm States 10101..10180 (PDU 0x0064 .. 0x00B3) */
    if (address >= MODBUS_DI_OFFSET_ALARMS &&
        (address + quantity) <= (MODBUS_DI_OFFSET_ALARMS + MODBUS_DI_COUNT_ALARMS)) {
        const alarm_descriptor_t *catalog[CONFIG_ALARM_MANAGER_MAX_ALARMS];
        size_t cat_count = app_facade_alarm_get_catalog(catalog, CONFIG_ALARM_MANAGER_MAX_ALARMS);

        app_facade_alarm_item_t act_items[CONFIG_ALARM_MANAGER_MAX_ALARMS];
        size_t act_count = app_facade_alarm_get_active_list(act_items, CONFIG_ALARM_MANAGER_MAX_ALARMS);

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t alarm_idx = (address + i) - MODBUS_DI_OFFSET_ALARMS;
            bool is_active = false;

            if (alarm_idx < cat_count && catalog[alarm_idx] != NULL) {
                uint16_t target_id = catalog[alarm_idx]->alarm_id;
                for (size_t k = 0U; k < act_count; k++) {
                    if (act_items[k].alarm_id == target_id &&
                        act_items[k].state != ALARM_STATE_INACTIVE) {
                        is_active = true;
                        break;
                    }
                }
            }
            nmbs_bitfield_write(inputs_out, i, is_active);
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 3: HVAC Units Tri-State Flags 10201..10224 (PDU 0x00C8 .. 0x00DF) */
    if (address >= MODBUS_DI_OFFSET_HVAC_TRISTATE &&
        (address + quantity) <= (MODBUS_DI_OFFSET_HVAC_TRISTATE + MODBUS_DI_COUNT_HVAC_TRISTATE)) {
        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t rel_idx = (address + i) - MODBUS_DI_OFFSET_HVAC_TRISTATE;
            uint8_t u = (uint8_t)(rel_idx / 3U);
            uint8_t r = (uint8_t)(rel_idx % 3U);
            bool val = false;

            if (u < HVAC_MAX_UNITS_TOTAL) {
                if (r == 0U) {
                    val = hstat.units[u].tri_state.relay_aa;
                } else if (r == 1U) {
                    val = hstat.units[u].tri_state.relay_sm;
                } else {
                    val = hstat.units[u].tri_state.relay_g;
                }
            }
            nmbs_bitfield_write(inputs_out, i, val);
        }
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

/* ========================================================================= */
/* 2. COILS (Function Codes 0x01, 0x05, 0x0F)                                 */
/* ========================================================================= */

nmbs_error adapter_modbus_tcp_on_read_coils(uint16_t address, uint16_t quantity,
                                            nmbs_bitfield coils_out, uint8_t unit_id,
                                            void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (quantity == 0U) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
    }

    /* Section 1: Relays AA/SM/G Overrides 00001..00024 (PDU 0x0000 .. 0x0017) */
    if (address < MODBUS_COIL_COUNT_HVAC_OVERRIDES &&
        (address + quantity) <= MODBUS_COIL_COUNT_HVAC_OVERRIDES) {
        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t rel_idx = address + i;
            uint8_t u = (uint8_t)(rel_idx / 3U);
            uint8_t r = (uint8_t)(rel_idx % 3U);
            bool val = false;

            if (u < HVAC_MAX_UNITS_TOTAL) {
                if (r == 0U) {
                    val = hstat.units[u].tri_state.relay_aa;
                } else if (r == 1U) {
                    val = hstat.units[u].tri_state.relay_sm;
                } else {
                    val = hstat.units[u].tri_state.relay_g;
                }
            }
            nmbs_bitfield_write(coils_out, i, val);
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 2: Buzzer Mute Toggle 00050 (PDU 0x0031) */
    if (address == MODBUS_COIL_OFFSET_BUZZER_MUTE && quantity == 1U) {
        bool muted = app_facade_alarm_is_buzzer_muted();
        nmbs_bitfield_write(coils_out, 0, muted);
        return NMBS_ERROR_NONE;
    }

    /* Section 3: Force Rotation Pulses 00060..00063 (PDU 0x003B .. 0x003E) */
    if (address >= MODBUS_COIL_OFFSET_FORCE_ROTATION &&
        (address + quantity) <= (MODBUS_COIL_OFFSET_FORCE_ROTATION + MODBUS_COIL_COUNT_FORCE_ROTATION)) {
        for (uint16_t i = 0U; i < quantity; i++) {
            nmbs_bitfield_write(coils_out, i, false);
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 4: Storage Save All 00070 (PDU 0x0045) */
    if (address == MODBUS_COIL_OFFSET_STORAGE_SAVE_ALL && quantity == 1U) {
        nmbs_bitfield_write(coils_out, 0, false);
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

nmbs_error adapter_modbus_tcp_on_write_single_coil(uint16_t address, bool value,
                                                   uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    /* Section 1: Relays AA/SM/G Overrides 00001..00024 */
    if (address < MODBUS_COIL_COUNT_HVAC_OVERRIDES) {
        uint8_t u = (uint8_t)(address / 3U);
        uint8_t r = (uint8_t)(address % 3U);

        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        bool aa = hstat.units[u].tri_state.relay_aa;
        bool g = hstat.units[u].tri_state.relay_g;

        if (r == 0U) {
            aa = value;
        } else if (r == 2U) {
            g = value;
        }

        bool enable = aa || g;
        err = app_facade_hvac_set_unit_manual(u, enable, aa, g);
        return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }

    /* Section 2: Buzzer Mute Toggle 00050 */
    if (address == MODBUS_COIL_OFFSET_BUZZER_MUTE) {
        int err = app_facade_alarm_set_buzzer_mute(value);
        return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }

    /* Section 3: Force Rotation Pulses 00060..00063 */
    if (address >= MODBUS_COIL_OFFSET_FORCE_ROTATION &&
        address < (MODBUS_COIL_OFFSET_FORCE_ROTATION + MODBUS_COIL_COUNT_FORCE_ROTATION)) {
        if (value) {
            uint8_t group_id = (uint8_t)(address - MODBUS_COIL_OFFSET_FORCE_ROTATION);
            int err = app_facade_hvac_force_unit_rotation(group_id);
            return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 4: Storage Save All 00070 */
    if (address == MODBUS_COIL_OFFSET_STORAGE_SAVE_ALL) {
        if (value) {
            int err = app_facade_storage_save_all();
            return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

nmbs_error adapter_modbus_tcp_on_write_multiple_coils(uint16_t address, uint16_t quantity,
                                                      const nmbs_bitfield coils,
                                                      uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    for (uint16_t i = 0U; i < quantity; i++) {
        bool val = nmbs_bitfield_read(coils, i);
        nmbs_error err = adapter_modbus_tcp_on_write_single_coil(address + i, val, unit_id, arg);
        if (err != NMBS_ERROR_NONE) {
            return err;
        }
    }
    return NMBS_ERROR_NONE;
}

/* ========================================================================= */
/* 3. INPUT REGISTERS (Function Code 0x04)                                   */
/* ========================================================================= */

nmbs_error adapter_modbus_tcp_on_read_input_registers(uint16_t address, uint16_t quantity,
                                                      uint16_t *registers_out,
                                                      uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    adapter_modbus_tcp_server_t *self = (adapter_modbus_tcp_server_t *)arg;
    modbus_word_order_t order = self ? self->word_order : MODBUS_WORD_ORDER_BIG_ENDIAN;

    if (quantity == 0U) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
    }

    /* Section 1: Sensor Matrix Temperatures 30001..30064 (PDU 0x0000 .. 0x003F) */
    if (address < MODBUS_IR_COUNT_SENSOR_MATRIX &&
        (address + quantity) <= MODBUS_IR_COUNT_SENSOR_MATRIX) {
        sensor_snapshot_msg_t ssnap;
        int err = app_facade_sensor_get_snapshot(&ssnap);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t ch = address + i;
            if ((ssnap.valid_mask & (1ULL << ch)) != 0ULL) {
                registers_out[i] = (uint16_t)(int16_t)roundf(ssnap.temperatures[ch] * 10.0f);
            } else {
                registers_out[i] = 0x7FFFU;
            }
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 2: Psychrometrics Return RH 30070..30071 (PDU 0x0045 .. 0x0046) */
    if (address >= MODBUS_IR_OFFSET_RETURN_RH &&
        (address + quantity) <= (MODBUS_IR_OFFSET_RETURN_RH + MODBUS_IR_COUNT_RETURN_RH)) {
        thermal_energy_capture_t cap;
        int err = app_facade_thermal_get_capture(&cap);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            if (reg == MODBUS_IR_OFFSET_RETURN_RH) {
                registers_out[i] = (uint16_t)(int16_t)roundf(cap.humidity_return_pct * 10.0f);
            } else {
                registers_out[i] = (cap.humidity_quality == QUALITY_VALID) ? 1U : 0U;
            }
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 3: 3-Phase Electrical Parameters 30101..30130 (PDU 0x0064 .. 0x0081) */
    if (address >= MODBUS_IR_OFFSET_ELECTRICAL &&
        (address + quantity) <= (MODBUS_IR_OFFSET_ELECTRICAL + MODBUS_IR_COUNT_ELECTRICAL)) {
        thermal_energy_capture_t cap;
        int err = app_facade_thermal_get_capture(&cap);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        uint32_t kwh_x10 = (uint32_t)round(cap.total_active_energy_kwh * 10.0);

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            switch (reg) {
            case 100: val = (uint16_t)roundf(cap.voltage_v[0] * 10.0f); break;
            case 101: val = (uint16_t)roundf(cap.voltage_v[1] * 10.0f); break;
            case 102: val = (uint16_t)roundf(cap.voltage_v[2] * 10.0f); break;
            case 103: val = (uint16_t)roundf(cap.current_a[0] * 100.0f); break;
            case 104: val = (uint16_t)roundf(cap.current_a[1] * 100.0f); break;
            case 105: val = (uint16_t)roundf(cap.current_a[2] * 100.0f); break;
            case 106: val = (uint16_t)roundf(cap.total_active_power_kw * 10.0f); break;
            case 107: val = (uint16_t)(int16_t)roundf(cap.power_factor * 1000.0f); break;
            case 108: val = 600U; break; /* 60.0 Hz */
            case 109:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)(kwh_x10 & 0xFFFFU) : (uint16_t)((kwh_x10 >> 16) & 0xFFFFU);
                break;
            case 110:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)((kwh_x10 >> 16) & 0xFFFFU) : (uint16_t)(kwh_x10 & 0xFFFFU);
                break;
            default: val = 0U; break;
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 4: Thermodynamic Metrics 30201..30230 (PDU 0x00C8 .. 0x00E5) */
    if (address >= MODBUS_IR_OFFSET_THERMODYNAMIC &&
        (address + quantity) <= (MODBUS_IR_OFFSET_THERMODYNAMIC + MODBUS_IR_COUNT_THERMODYNAMIC)) {
        thermal_energy_result_t res;
        int err = app_facade_thermal_get_result(&res);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            switch (reg) {
            case 200: val = (uint16_t)roundf(res.cop_real * 100.0f); break;
            case 201: val = (uint16_t)roundf(res.cop_sensible * 100.0f); break;
            case 202: val = (uint16_t)roundf(res.cop_carnot * 100.0f); break;
            case 203: val = (uint16_t)roundf(res.q_sensible_total * 10.0f); break;
            case 204: val = (uint16_t)roundf(res.q_latente_total * 10.0f); break;
            case 205: val = (uint16_t)roundf(res.q_total * 10.0f); break;
            case 206: val = (uint16_t)roundf(res.eta_ii_pct * 10.0f); break;
            case 207: val = (uint16_t)roundf(res.m_dot_da_total * 100.0f); break;
            case 208: val = (uint16_t)roundf(res.m_dot_da_total * 3000.0f); break;
            case 209: val = (uint16_t)(int16_t)roundf(res.t_inj_mean * 10.0f); break;
            case 210: val = (uint16_t)(int16_t)roundf(res.t_ret_mean * 10.0f); break;
            case 211: val = (uint16_t)(int16_t)roundf(res.delta_t_clima_mean * 10.0f); break;
            case 212: val = (uint16_t)(int16_t)roundf(res.t_sala_mean * 10.0f); break;
            case 213: val = (uint16_t)(int16_t)roundf(res.t_sala_max * 10.0f); break;
            case 214: val = (uint16_t)(int16_t)roundf(res.t_sala_min * 10.0f); break;
            case 215: val = (uint16_t)(int16_t)roundf(res.delta_t_bypass * 10.0f); break;
            case 216: val = (uint16_t)roundf(res.shf * 1000.0f); break;
            case 217: val = (uint16_t)roundf(res.m_dot_condensate_lph * 10.0f); break;
            default: val = 0U; break;
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 5: System Telemetry & Diagnostics 30301..30320 (PDU 0x012C .. 0x013F) */
    if (address >= MODBUS_IR_OFFSET_SYSTEM_DIAG &&
        (address + quantity) <= (MODBUS_IR_OFFSET_SYSTEM_DIAG + MODBUS_IR_COUNT_SYSTEM_DIAG)) {
        app_facade_system_info_t sys_info;
        (void)app_facade_system_get_info(&sys_info);

        storage_fs_card_status_t card_stat = STORAGE_FS_CARD_UNKNOWN;
        storage_fs_volume_info_t vol = {0};
        (void)app_facade_sd_get_status(&card_stat, &vol);

        uint32_t eth_ip = 0xC0A80164U; /* 192.168.1.100 */
        uint32_t wifi_ip = 0xC0A80401U; /* 192.168.4.1 */
        uint32_t uptime_s = (uint32_t)sys_info.uptime_seconds;
        uint32_t total_mb = (uint32_t)(vol.total_bytes / (1024ULL * 1024ULL));
        uint32_t free_mb = (uint32_t)(vol.free_bytes / (1024ULL * 1024ULL));

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            switch (reg) {
            case 300:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)(uptime_s & 0xFFFFU) : (uint16_t)((uptime_s >> 16) & 0xFFFFU);
                break;
            case 301:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)((uptime_s >> 16) & 0xFFFFU) : (uint16_t)(uptime_s & 0xFFFFU);
                break;
            case 302: val = (uint16_t)((eth_ip >> 16) & 0xFFFFU); break;
            case 303: val = (uint16_t)(eth_ip & 0xFFFFU); break;
            case 304: val = (uint16_t)((wifi_ip >> 16) & 0xFFFFU); break;
            case 305: val = (uint16_t)(wifi_ip & 0xFFFFU); break;
            case 306:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)(total_mb & 0xFFFFU) : (uint16_t)((total_mb >> 16) & 0xFFFFU);
                break;
            case 307:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)((total_mb >> 16) & 0xFFFFU) : (uint16_t)(total_mb & 0xFFFFU);
                break;
            case 308:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)(free_mb & 0xFFFFU) : (uint16_t)((free_mb >> 16) & 0xFFFFU);
                break;
            case 309:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)((free_mb >> 16) & 0xFFFFU) : (uint16_t)(free_mb & 0xFFFFU);
                break;
            case 310:
                val = (card_stat == STORAGE_FS_CARD_MOUNTED) ? 3U :
                      ((card_stat != STORAGE_FS_CARD_NOT_INSERTED) ? 1U : 0U);
                break;
            default: val = 0U; break;
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 6: Active Alarm Diagnostics 30401..30410 (PDU 0x0190 .. 0x0199) */
    if (address >= MODBUS_IR_OFFSET_ALARM_DIAG &&
        (address + quantity) <= (MODBUS_IR_OFFSET_ALARM_DIAG + MODBUS_IR_COUNT_ALARM_DIAG)) {
        app_facade_alarm_status_t astat;
        (void)app_facade_alarm_get_status(&astat);

        const alarm_descriptor_t *catalog[CONFIG_ALARM_MANAGER_MAX_ALARMS];
        size_t cat_count = app_facade_alarm_get_catalog(catalog, CONFIG_ALARM_MANAGER_MAX_ALARMS);

        app_facade_alarm_item_t act_items[CONFIG_ALARM_MANAGER_MAX_ALARMS];
        size_t act_count = app_facade_alarm_get_active_list(act_items, CONFIG_ALARM_MANAGER_MAX_ALARMS);

        uint16_t active_mask[5] = {0};
        for (size_t a = 0U; a < cat_count && a < 80U; a++) {
            if (catalog[a] == NULL) continue;
            uint16_t aid = catalog[a]->alarm_id;
            for (size_t k = 0U; k < act_count; k++) {
                if (act_items[k].alarm_id == aid && act_items[k].state != ALARM_STATE_INACTIVE) {
                    active_mask[a / 16U] |= (1U << (a % 16U));
                    break;
                }
            }
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            switch (reg) {
            case 400: val = (uint16_t)astat.active_count; break;
            case 401:
                val = (uint16_t)(astat.active_count > astat.acknowledged_count ?
                                (astat.active_count - astat.acknowledged_count) : 0U);
                break;
            case 402: val = (uint16_t)astat.highest_level; break;
            case 403: val = (uint16_t)(astat.buzzer_muted ? 0U : (astat.active_count > 0U ? 1U : 2U)); break;
            case 404: val = active_mask[0]; break;
            case 405: val = active_mask[1]; break;
            case 406: val = active_mask[2]; break;
            case 407: val = active_mask[3]; break;
            case 408: val = active_mask[4]; break;
            default: val = 0U; break;
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

/* ========================================================================= */
/* 4. HOLDING REGISTERS (Function Codes 0x03, 0x06, 0x10)                     */
/* ========================================================================= */

nmbs_error adapter_modbus_tcp_on_read_holding_registers(uint16_t address, uint16_t quantity,
                                                        uint16_t *registers_out,
                                                        uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    adapter_modbus_tcp_server_t *self = (adapter_modbus_tcp_server_t *)arg;
    modbus_word_order_t order = self ? self->word_order : MODBUS_WORD_ORDER_BIG_ENDIAN;

    if (quantity == 0U) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
    }

    /* Section 1: HVAC Setpoints 40001..40040 (PDU 0x0000 .. 0x0027) */
    if (address < MODBUS_HR_COUNT_HVAC_SETPOINTS &&
        (address + quantity) <= MODBUS_HR_COUNT_HVAC_SETPOINTS) {
        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint8_t g = (uint8_t)(reg / 10U);
            uint8_t r = (uint8_t)(reg % 10U);
            uint16_t val = 0U;

            if (g < HVAC_MAX_GROUPS) {
                switch (r) {
                case 0: val = (uint16_t)(int16_t)roundf(hstat.groups[g].temp_min * 10.0f); break;
                case 1: val = (uint16_t)(int16_t)roundf(hstat.groups[g].temp_max * 10.0f); break;
                case 2: val = (uint16_t)(int16_t)roundf(hstat.groups[g].temp_alarm * 10.0f); break;
                case 3: val = (uint16_t)(int16_t)roundf(hstat.groups[g].temp_bypass * 10.0f); break;
                default: val = 0U; break;
                }
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 2: HVAC Group Configuration 40041..40060 (PDU 0x0028 .. 0x003B) */
    if (address >= MODBUS_HR_OFFSET_HVAC_CONFIG &&
        (address + quantity) <= (MODBUS_HR_OFFSET_HVAC_CONFIG + MODBUS_HR_COUNT_HVAC_CONFIG)) {
        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t idx = reg - MODBUS_HR_OFFSET_HVAC_CONFIG;
            uint8_t g = (uint8_t)(idx / 5U);
            uint8_t r = (uint8_t)(idx % 5U);
            uint16_t val = 0U;

            if (g < HVAC_MAX_GROUPS) {
                switch (r) {
                case 0: val = (uint16_t)hstat.groups[g].state; break;
                case 1: val = (uint16_t)hstat.groups[g].active_turn_count; break;
                case 2: val = (uint16_t)hstat.groups[g].unit_mask; break;
                case 3: val = (uint16_t)hstat.groups[g].rotation_interval_days; break;
                case 4:
                    val = (uint16_t)((hstat.groups[g].enable_fan_delay ? 0x8000U : 0U) |
                                     (hstat.groups[g].fan_delay_s & 0x7FFFU));
                    break;
                default: val = 0U; break;
                }
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 3: RTC Calendar & UNIX Epoch 40101..40110 (PDU 0x0064 .. 0x006D) */
    if (address >= MODBUS_HR_OFFSET_RTC &&
        (address + quantity) <= (MODBUS_HR_OFFSET_RTC + MODBUS_HR_COUNT_RTC)) {
        rtc_date_time_t dt;
        (void)app_facade_time_get_datetime(&dt);
        int64_t epoch_s = 0;
        uint16_t sub_ms = 0U;
        (void)app_facade_time_get_unix(&epoch_s, &sub_ms);
        rtc_sync_status_t sync_stat;
        (void)app_facade_time_get_status(&sync_stat);

        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            switch (reg) {
            case 100: val = dt.year; break;
            case 101: val = dt.month; break;
            case 102: val = dt.day; break;
            case 103: val = dt.hours; break;
            case 104: val = dt.minutes; break;
            case 105: val = dt.seconds; break;
            case 106:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)(epoch_s & 0xFFFF) : (uint16_t)((epoch_s >> 16) & 0xFFFF);
                break;
            case 107:
                val = (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) ?
                      (uint16_t)((epoch_s >> 16) & 0xFFFF) : (uint16_t)(epoch_s & 0xFFFF);
                break;
            case 108: val = sub_ms; break;
            case 109: val = (uint16_t)sync_stat.sync_state; break;
            default: val = 0U; break;
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    /* Section 4: Supervisory Command Execution 40201..40205 (PDU 0x00C8 .. 0x00CC) */
    if (address >= MODBUS_HR_OFFSET_SUPERVISORY &&
        (address + quantity) <= (MODBUS_HR_OFFSET_SUPERVISORY + MODBUS_HR_COUNT_SUPERVISORY)) {
        for (uint16_t i = 0U; i < quantity; i++) {
            uint16_t reg = address + i;
            uint16_t val = 0U;
            if (self != NULL) {
                switch (reg) {
                case 200: val = self->supervisory_opcode; break;
                case 201: val = self->supervisory_arg; break;
                case 202: val = self->supervisory_status; break;
                case 203: val = self->supervisory_last_opcode; break;
                case 204: val = self->supervisory_last_errno; break;
                default: val = 0U; break;
                }
            }
            registers_out[i] = val;
        }
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

static int execute_supervisory_command(adapter_modbus_tcp_server_t *self, uint16_t opcode, uint16_t arg)
{
    int rc = 0;
    switch ((modbus_supervisory_cmd_t)opcode) {
    case MODBUS_SUPERVISORY_CMD_NONE:
        rc = 0;
        break;
    case MODBUS_SUPERVISORY_CMD_ACK_ALARM_ID:
        rc = app_facade_alarm_ack(arg);
        break;
    case MODBUS_SUPERVISORY_CMD_ACK_ALL_ALARMS:
        rc = app_facade_alarm_ack(0U);
        break;
    case MODBUS_SUPERVISORY_CMD_RESET_LATCH:
        rc = app_facade_alarm_reset_latch(arg);
        break;
    case MODBUS_SUPERVISORY_CMD_REBOOT:
        rc = app_facade_system_reboot();
        break;
    case MODBUS_SUPERVISORY_CMD_STORAGE_SAVE_ALL:
        rc = app_facade_storage_save_all();
        break;
    case MODBUS_SUPERVISORY_CMD_RECALIBRATE_BASELINE:
        rc = app_facade_thermal_trigger_recalibration();
        break;
    default:
        rc = -EINVAL;
        break;
    }

    if (self != NULL) {
        self->supervisory_last_opcode = opcode;
        self->supervisory_last_errno = (uint16_t)(rc < 0 ? -rc : 0);
        self->supervisory_status = (rc == 0) ? 1U : 2U;
    }
    return rc;
}

nmbs_error adapter_modbus_tcp_on_write_single_register(uint16_t address, uint16_t value,
                                                       uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    adapter_modbus_tcp_server_t *self = (adapter_modbus_tcp_server_t *)arg;

    /* Section 1: HVAC Setpoints 40001..40040 */
    if (address < MODBUS_HR_COUNT_HVAC_SETPOINTS) {
        uint8_t g = (uint8_t)(address / 10U);
        uint8_t r = (uint8_t)(address % 10U);

        if (g >= HVAC_MAX_GROUPS || r > 3U) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }

        app_facade_hvac_status_t hstat;
        int err = app_facade_hvac_get_status(&hstat);
        if (err != 0) {
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        }

        float tmin = hstat.groups[g].temp_min;
        float tmax = hstat.groups[g].temp_max;
        float talm = hstat.groups[g].temp_alarm;
        float tbyp = hstat.groups[g].temp_bypass;

        float new_t = (float)(int16_t)value / 10.0f;
        if (new_t < -20.0f || new_t > 80.0f) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }

        if (r == 0U) tmin = new_t;
        else if (r == 1U) tmax = new_t;
        else if (r == 2U) talm = new_t;
        else if (r == 3U) tbyp = new_t;

        err = app_facade_hvac_set_group_thermal_bounds(g, tmin, tmax, talm, tbyp);
        return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }

    /* Section 2: HVAC Group Configuration 40041..40060 */
    if (address >= MODBUS_HR_OFFSET_HVAC_CONFIG &&
        address < (MODBUS_HR_OFFSET_HVAC_CONFIG + MODBUS_HR_COUNT_HVAC_CONFIG)) {
        uint16_t idx = address - MODBUS_HR_OFFSET_HVAC_CONFIG;
        uint8_t g = (uint8_t)(idx / 5U);
        uint8_t r = (uint8_t)(idx % 5U);

        if (g >= HVAC_MAX_GROUPS) {
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }

        int err = 0;
        switch (r) {
        case 0:
            if (value > 5U) return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
            err = app_facade_hvac_set_group_mode(g, (hvac_mode_t)value);
            break;
        case 1:
            if (value < 1U || value > 8U) return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
            err = app_facade_hvac_configure_group_units(g, (uint8_t)value, 0U);
            break;
        case 2:
            if (value == 0U || value > 0xFFU) return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
            err = app_facade_hvac_configure_group_units(g, 0U, (uint8_t)value);
            break;
        case 3:
            if (value < 1U || value > 365U) return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
            err = app_facade_hvac_set_group_rotation(g, value);
            break;
        case 4: {
            bool en = (value & 0x8000U) != 0U;
            uint16_t delay = (value & 0x7FFFU);
            err = app_facade_hvac_set_group_fan_control(g, en, delay);
            break;
        }
        default:
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
        return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }

    /* Section 3: RTC Calendar & UNIX Epoch 40101..40110 */
    if (address >= MODBUS_HR_OFFSET_RTC &&
        address < (MODBUS_HR_OFFSET_RTC + MODBUS_HR_COUNT_RTC)) {
        return NMBS_ERROR_NONE; /* Read-only or requires multi-word atomic block */
    }

    /* Section 4: Supervisory Command Execution 40201..40205 */
    if (address >= MODBUS_HR_OFFSET_SUPERVISORY &&
        address < (MODBUS_HR_OFFSET_SUPERVISORY + MODBUS_HR_COUNT_SUPERVISORY)) {
        if (address == 200U) {
            if (self != NULL) {
                self->supervisory_opcode = value;
                (void)execute_supervisory_command(self, self->supervisory_opcode, self->supervisory_arg);
            }
        } else if (address == 201U) {
            if (self != NULL) {
                self->supervisory_arg = value;
            }
        }
        return NMBS_ERROR_NONE;
    }

    return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
}

nmbs_error adapter_modbus_tcp_on_write_multiple_registers(uint16_t address, uint16_t quantity,
                                                          const uint16_t *registers,
                                                          uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    adapter_modbus_tcp_server_t *self = (adapter_modbus_tcp_server_t *)arg;

    /* Handle multi-register RTC setting */
    if (address == 100U && quantity >= 6U) {
        rtc_date_time_t dt = {0};
        dt.year = registers[0];
        dt.month = (uint8_t)registers[1];
        dt.day = (uint8_t)registers[2];
        dt.hours = (uint8_t)registers[3];
        dt.minutes = (uint8_t)registers[4];
        dt.seconds = (uint8_t)registers[5];
        int err = app_facade_time_set_datetime(&dt, RTC_TIME_SOURCE_MODBUS_TCP);
        return (err == 0) ? NMBS_ERROR_NONE : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
    }

    /* Handle multi-register Supervisory dispatch */
    if (address == 200U && quantity >= 2U) {
        if (self != NULL) {
            self->supervisory_opcode = registers[0];
            self->supervisory_arg = registers[1];
            (void)execute_supervisory_command(self, self->supervisory_opcode, self->supervisory_arg);
        }
        return NMBS_ERROR_NONE;
    }

    /* Default to iterative single register writes */
    for (uint16_t i = 0U; i < quantity; i++) {
        nmbs_error err = adapter_modbus_tcp_on_write_single_register(address + i, registers[i], unit_id, arg);
        if (err != NMBS_ERROR_NONE) {
            return err;
        }
    }
    return NMBS_ERROR_NONE;
}
