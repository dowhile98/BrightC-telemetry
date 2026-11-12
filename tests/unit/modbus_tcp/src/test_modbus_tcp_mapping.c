/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "common/modbus_tcp_types.h"
#include "interfaces/i_modbus_tcp_server.h"
#include "infrastructure/adapters/adapter_modbus_tcp_server.h"
#include "application/active_objects/ao_modbus_tcp.h"
#include "application/services/app_facade.h"

/* ========================================================================= */
/* Mock / Stub State for app_facade_*                                        */
/* ========================================================================= */

static di_snapshot_msg_t s_mock_di_snapshot;
static app_facade_alarm_status_t s_mock_alarm_status;
static app_facade_alarm_item_t s_mock_active_items[10];
static size_t s_mock_active_count = 0;
static alarm_descriptor_t s_mock_catalog_items[10];
static const alarm_descriptor_t *s_mock_catalog[10];

static app_facade_hvac_status_t s_mock_hvac_status;
static sensor_snapshot_msg_t s_mock_sensor_snapshot;
static thermal_energy_capture_t s_mock_thermal_capture;
static thermal_energy_result_t s_mock_thermal_result;
static storage_fs_card_status_t s_mock_card_status = STORAGE_FS_CARD_MOUNTED;
static storage_fs_volume_info_t s_mock_vol_info = {
    .total_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
    .free_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL,
};
static rtc_date_time_t s_mock_rtc_datetime;
static int64_t s_mock_rtc_unix = 1773000000LL;
static uint16_t s_mock_rtc_sub_ms = 0;
static rtc_sync_status_t s_mock_sync_status = { .sync_state = RTC_SYNC_STATE_SYNCHRONIZED };

static hvac_mode_t s_mock_group0_mode = HVAC_MODE_AUTO;
static uint16_t s_mock_group0_rotation_days = 0;
static bool s_mock_buzzer_muted = false;
static uint8_t s_last_force_rotation_group = 0xFF;
static bool s_storage_saved = false;
static uint16_t s_last_acked_alarm = 0xFFFF;
static bool s_latch_reset_called = false;
static bool s_reboot_called = false;

/* Mock implementations of app_facade APIs */

int app_facade_di_get_snapshot(di_snapshot_msg_t *out_snapshot)
{
    if (out_snapshot == NULL) return -EINVAL;
    *out_snapshot = s_mock_di_snapshot;
    return 0;
}

int app_facade_alarm_get_status(app_facade_alarm_status_t *out_status)
{
    if (out_status == NULL) return -EINVAL;
    *out_status = s_mock_alarm_status;
    return 0;
}

size_t app_facade_alarm_get_catalog(const alarm_descriptor_t *out_catalog[], size_t max_count)
{
    size_t count = (max_count < 10) ? max_count : 10;
    for (size_t i = 0; i < count; i++) {
        out_catalog[i] = s_mock_catalog[i];
    }
    return count;
}

size_t app_facade_alarm_get_active_list(app_facade_alarm_item_t *out_items, size_t max_items)
{
    size_t count = (max_items < s_mock_active_count) ? max_items : s_mock_active_count;
    for (size_t i = 0; i < count; i++) {
        out_items[i] = s_mock_active_items[i];
    }
    return count;
}

int app_facade_alarm_ack(uint16_t alarm_id)
{
    s_last_acked_alarm = alarm_id;
    return 0;
}

int app_facade_alarm_reset_latch(uint16_t alarm_id)
{
    ARG_UNUSED(alarm_id);
    s_latch_reset_called = true;
    return 0;
}

int app_facade_alarm_set_buzzer_mute(bool muted)
{
    s_mock_buzzer_muted = muted;
    return 0;
}

bool app_facade_alarm_is_buzzer_muted(void)
{
    return s_mock_buzzer_muted;
}

int app_facade_hvac_get_status(app_facade_hvac_status_t *out_status)
{
    if (out_status == NULL) return -EINVAL;
    *out_status = s_mock_hvac_status;
    return 0;
}

int app_facade_hvac_force_unit_rotation(uint8_t group_id)
{
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    s_last_force_rotation_group = group_id;
    return 0;
}

int app_facade_hvac_set_setpoint(uint8_t group_id, int16_t cool_target_x10, int16_t heat_target_x10)
{
    ARG_UNUSED(heat_target_x10);
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    s_mock_hvac_status.groups[group_id].temp_max = (float)cool_target_x10 / 10.0f;
    return 0;
}

int app_facade_hvac_set_group_thermal_bounds(uint8_t group_id, float t_min, float t_max,
                                             float t_alarm, float t_bypass)
{
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    s_mock_hvac_status.groups[group_id].temp_min = t_min;
    s_mock_hvac_status.groups[group_id].temp_max = t_max;
    s_mock_hvac_status.groups[group_id].temp_alarm = t_alarm;
    s_mock_hvac_status.groups[group_id].temp_bypass = t_bypass;
    return 0;
}

int app_facade_hvac_set_mode(uint8_t group_id, hvac_mode_t mode)
{
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    if (group_id == 0) s_mock_group0_mode = mode;
    return 0;
}

int app_facade_hvac_set_group_mode(uint8_t group_id, hvac_mode_t mode)
{
    return app_facade_hvac_set_mode(group_id, mode);
}

int app_facade_hvac_set_group_rotation(uint8_t group_id, uint16_t days)
{
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    if (group_id == 0) s_mock_group0_rotation_days = days;
    return 0;
}

int app_facade_hvac_set_group_fan_control(uint8_t group_id, bool enable, uint16_t fan_delay_s)
{
    ARG_UNUSED(enable);
    ARG_UNUSED(fan_delay_s);
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    return 0;
}

int app_facade_hvac_configure_group_units(uint8_t group_id, uint8_t active_turn_count, uint8_t unit_mask)
{
    if (group_id >= HVAC_MAX_GROUPS) return -EINVAL;
    s_mock_hvac_status.groups[group_id].active_turn_count = active_turn_count;
    s_mock_hvac_status.groups[group_id].unit_mask = unit_mask;
    return 0;
}

int app_facade_sensor_get_snapshot(sensor_snapshot_msg_t *out_snapshot)
{
    if (out_snapshot == NULL) return -EINVAL;
    *out_snapshot = s_mock_sensor_snapshot;
    return 0;
}

int app_facade_thermal_get_capture(thermal_energy_capture_t *out_capture)
{
    if (out_capture == NULL) return -EINVAL;
    *out_capture = s_mock_thermal_capture;
    return 0;
}

int app_facade_thermal_get_result(thermal_energy_result_t *out_result)
{
    if (out_result == NULL) return -EINVAL;
    *out_result = s_mock_thermal_result;
    return 0;
}

int app_facade_thermal_trigger_recalibration(void)
{
    return 0;
}

int app_facade_time_get_datetime(rtc_date_time_t *out_datetime)
{
    if (out_datetime == NULL) return -EINVAL;
    *out_datetime = s_mock_rtc_datetime;
    return 0;
}

int app_facade_time_get_unix(int64_t *out_epoch_s, uint16_t *out_sub_ms)
{
    if (out_epoch_s == NULL) return -EINVAL;
    *out_epoch_s = s_mock_rtc_unix;
    if (out_sub_ms != NULL) *out_sub_ms = s_mock_rtc_sub_ms;
    return 0;
}

int app_facade_time_get_status(rtc_sync_status_t *out_status)
{
    if (out_status == NULL) return -EINVAL;
    *out_status = s_mock_sync_status;
    return 0;
}

int app_facade_time_set_datetime(const rtc_date_time_t *dt, rtc_time_source_t source)
{
    ARG_UNUSED(source);
    if (dt == NULL) return -EINVAL;
    s_mock_rtc_datetime = *dt;
    return 0;
}

int app_facade_time_set_unix(int64_t epoch_s, rtc_time_source_t source)
{
    ARG_UNUSED(source);
    s_mock_rtc_unix = epoch_s;
    return 0;
}

int app_facade_sd_get_status(storage_fs_card_status_t *out_status, storage_fs_volume_info_t *out_vol)
{
    if (out_status != NULL) *out_status = s_mock_card_status;
    if (out_vol != NULL) *out_vol = s_mock_vol_info;
    return 0;
}

int app_facade_storage_save_all(void)
{
    s_storage_saved = true;
    return 0;
}

int app_facade_hvac_set_unit_manual(uint8_t unit_id, bool enable, bool run_compressor, bool run_fan)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(enable);
    ARG_UNUSED(run_compressor);
    ARG_UNUSED(run_fan);
    return 0;
}

int app_facade_system_get_info(app_facade_system_info_t *out_info)
{
    if (out_info == NULL) return -EINVAL;
    memset(out_info, 0, sizeof(*out_info));
    out_info->uptime_seconds = 123456ULL;
    return 0;
}

int app_facade_system_reboot(void)
{
    s_reboot_called = true;
    return 0;
}

int app_facade_modbus_tcp_get_stats(modbus_tcp_server_stats_t *out_stats)
{
    if (out_stats == NULL) return -EINVAL;
    memset(out_stats, 0, sizeof(*out_stats));
    return 0;
}

/* ========================================================================= */
/* Test Setup Fixture                                                        */
/* ========================================================================= */

static adapter_modbus_tcp_server_t s_adapter;
static modbus_tcp_server_config_t s_config;

static void *modbus_tcp_setup(void)
{
    memset(&s_adapter, 0, sizeof(s_adapter));
    s_config.port = 502;
    s_config.max_clients = 4;
    s_config.inactivity_timeout_ms = 30000;
    s_config.word_order = MODBUS_WORD_ORDER_BIG_ENDIAN;

    zassert_equal(adapter_modbus_tcp_server_init(&s_adapter, &s_config), 0, "Adapter init failed");

    /* Reset mock state */
    memset(&s_mock_di_snapshot, 0, sizeof(s_mock_di_snapshot));
    memset(&s_mock_alarm_status, 0, sizeof(s_mock_alarm_status));
    memset(&s_mock_active_items, 0, sizeof(s_mock_active_items));
    s_mock_active_count = 0;
    memset(&s_mock_catalog_items, 0, sizeof(s_mock_catalog_items));
    for (size_t i = 0; i < 10; i++) {
        s_mock_catalog_items[i].alarm_id = (uint16_t)(100 + i);
        s_mock_catalog[i] = &s_mock_catalog_items[i];
    }
    memset(&s_mock_hvac_status, 0, sizeof(s_mock_hvac_status));
    memset(&s_mock_sensor_snapshot, 0, sizeof(s_mock_sensor_snapshot));
    memset(&s_mock_thermal_capture, 0, sizeof(s_mock_thermal_capture));
    memset(&s_mock_thermal_result, 0, sizeof(s_mock_thermal_result));
    memset(&s_mock_rtc_datetime, 0, sizeof(s_mock_rtc_datetime));
    s_mock_rtc_unix = 1773000000U;
    s_mock_group0_mode = HVAC_MODE_AUTO;
    s_mock_group0_rotation_days = 0;
    s_mock_buzzer_muted = false;
    s_last_force_rotation_group = 0xFF;
    s_storage_saved = false;
    s_last_acked_alarm = 0xFFFF;
    s_latch_reset_called = false;
    s_reboot_called = false;

    return NULL;
}

ZTEST_SUITE(modbus_tcp_mapping_suite, NULL, modbus_tcp_setup, NULL, NULL, NULL);

/* ========================================================================= */
/* Test 1: Discrete Inputs Mapping                                           */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_discrete_inputs_mapping)
{
    uint8_t bitfield[16] = {0};

    /* 1.1 Digital Inputs 10001..10056 (PDU offset 0..55) */
    s_mock_di_snapshot.channel_count = 56;
    s_mock_di_snapshot.is_active[0] = true;
    s_mock_di_snapshot.is_active[1] = false;
    s_mock_di_snapshot.is_active[7] = true;
    s_mock_di_snapshot.is_active[55] = true;

    nmbs_error err = adapter_modbus_tcp_on_read_discrete_inputs(0, 56, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read DI 0..55 failed");
    zassert_true((bitfield[0] & 0x01) != 0, "DI 0 bit should be set");
    zassert_false((bitfield[0] & 0x02) != 0, "DI 1 bit should be clear");
    zassert_true((bitfield[0] & 0x80) != 0, "DI 7 bit should be set");
    zassert_true((bitfield[6] & 0x80) != 0, "DI 55 bit should be set");

    /* 1.2 Alarm states 10101..10180 (PDU offset 100..179) */
    memset(bitfield, 0, sizeof(bitfield));
    s_mock_active_items[0].alarm_id = 100;
    s_mock_active_items[0].state = ALARM_STATE_ACTIVE;
    s_mock_active_items[1].alarm_id = 102;
    s_mock_active_items[1].state = ALARM_STATE_ACKNOWLEDGED;
    s_mock_active_count = 2;

    err = adapter_modbus_tcp_on_read_discrete_inputs(100, 8, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read alarm states failed");
    zassert_equal(bitfield[0], 0x05, "Alarm mask mismatch: 0x%02X vs 0x05", bitfield[0]);

    /* 1.3 HVAC tri-state flags 10201..10224 (PDU offset 200..223) */
    memset(bitfield, 0, sizeof(bitfield));
    s_mock_hvac_status.units[0].tri_state.relay_aa = true;
    s_mock_hvac_status.units[0].tri_state.relay_sm = false;
    s_mock_hvac_status.units[0].tri_state.relay_g  = true;

    err = adapter_modbus_tcp_on_read_discrete_inputs(200, 3, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read HVAC tri-state failed");
    zassert_true((bitfield[0] & 0x01) != 0, "AA relay should be ON");
    zassert_false((bitfield[0] & 0x02) != 0, "SM relay should be OFF");
    zassert_true((bitfield[0] & 0x04) != 0, "G fan relay should be ON");

    /* 1.4 Invalid quantity or address */
    err = adapter_modbus_tcp_on_read_discrete_inputs(0, 0, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_EXCEPTION_ILLEGAL_DATA_VALUE, "Zero quantity should fail");

    err = adapter_modbus_tcp_on_read_discrete_inputs(900, 10, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "Out of range address should fail");
}

/* ========================================================================= */
/* Test 2: Coils Mapping & Commands                                          */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_coils_read_and_write)
{
    uint8_t bitfield[8] = {0};

    /* 2.1 Buzzer mute coil (offset 49, Modbus Ref 00050) */
    s_mock_buzzer_muted = false;
    nmbs_error err = adapter_modbus_tcp_on_read_coils(49, 1, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read coil 49 failed");
    zassert_equal(bitfield[0], 0, "Buzzer mute should be 0");

    /* Write single coil to mute */
    err = adapter_modbus_tcp_on_write_single_coil(49, true, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Write single coil 49 failed");
    zassert_true(s_mock_buzzer_muted, "Facade buzzer mute should be true");

    err = adapter_modbus_tcp_on_read_coils(49, 1, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read coil 49 failed");
    zassert_equal(bitfield[0], 0x01, "Buzzer mute should be 1");

    /* 2.2 Force rotation pulses (offsets 59..62, Groups 0..3) */
    err = adapter_modbus_tcp_on_write_single_coil(59 + 2, true, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Force rotation group 2 failed");
    zassert_equal(s_last_force_rotation_group, 2, "Force rotation group should be 2");

    /* 2.3 Storage save-all trigger (offset 69, Ref 00070) */
    s_storage_saved = false;
    err = adapter_modbus_tcp_on_write_single_coil(69, true, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Storage save all failed");
    zassert_true(s_storage_saved, "Storage save all should be called");

    /* Writing false to pulse coil should succeed as no-op */
    s_storage_saved = false;
    err = adapter_modbus_tcp_on_write_single_coil(69, false, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Writing false to pulse should succeed");
    zassert_false(s_storage_saved, "False pulse should not trigger storage save");

    /* 2.4 Invalid addresses */
    err = adapter_modbus_tcp_on_read_coils(500, 1, bitfield, 1, &s_adapter);
    zassert_equal(err, NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "Out of range coil read");

    err = adapter_modbus_tcp_on_write_single_coil(500, true, 1, &s_adapter);
    zassert_equal(err, NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "Out of range coil write");
}

/* ========================================================================= */
/* Test 3: Input Registers Mapping                                           */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_input_registers_mapping)
{
    uint16_t regs[16] = {0};

    /* 3.1 Sensor matrix temperatures in 0.1 °C (offset 0..63) */
    s_mock_sensor_snapshot.temperatures[0] = 25.5f; /* 255 in 0.1 °C */
    s_mock_sensor_snapshot.valid_mask |= (1ULL << 0);
    s_mock_sensor_snapshot.temperatures[63] = -5.0f; /* -50 in 0.1 °C */
    s_mock_sensor_snapshot.valid_mask |= (1ULL << 63);

    nmbs_error err = adapter_modbus_tcp_on_read_input_registers(0, 1, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read IR 0 failed");
    zassert_equal(regs[0], 255, "Channel 0 temp mismatch: %u vs 255", regs[0]);

    err = adapter_modbus_tcp_on_read_input_registers(63, 1, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read IR 63 failed");
    zassert_equal((int16_t)regs[0], -50, "Channel 63 temp mismatch: %d vs -50", (int16_t)regs[0]);

    /* Invalid sensor returns 0x7FFF */
    s_mock_sensor_snapshot.valid_mask &= ~(1ULL << 1);
    err = adapter_modbus_tcp_on_read_input_registers(1, 1, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read IR 1 failed");
    zassert_equal(regs[0], 0x7FFFU, "Invalid sensor should return 0x7FFF");

    /* 3.2 Return RH and valid (offset 69..70) */
    s_mock_thermal_capture.humidity_return_pct = 54.3f; /* 543 */
    s_mock_thermal_capture.humidity_quality = QUALITY_VALID;
    err = adapter_modbus_tcp_on_read_input_registers(69, 2, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read RH failed");
    zassert_equal(regs[0], 543, "RH mismatch: %u vs 543", regs[0]);
    zassert_equal(regs[1], 1, "RH valid flag should be 1");

    /* 3.3 Active alarm diagnostics (offset 400..409) */
    s_mock_alarm_status.active_count = 2;
    s_mock_active_items[0].alarm_id = 100;
    s_mock_active_items[0].state = ALARM_STATE_ACTIVE;
    s_mock_active_items[1].alarm_id = 101;
    s_mock_active_items[1].state = ALARM_STATE_ACKNOWLEDGED;
    s_mock_active_count = 2;

    err = adapter_modbus_tcp_on_read_input_registers(400, 3, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read alarm diagnostics failed");
    zassert_equal(regs[0], 2, "Active count mismatch: %u vs 2", regs[0]);
}

/* ========================================================================= */
/* Test 4: Holding Registers Mapping & Configuration Writes                  */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_holding_registers_mapping)
{
    uint16_t regs[16] = {0};

    /* 4.1 HVAC Group 0 Setpoints (offset 0..3: Tmin, Tmax, Talm, Tbyp in 0.1 °C) */
    s_mock_hvac_status.groups[0].temp_min = 20.0f;
    s_mock_hvac_status.groups[0].temp_max = 24.0f;
    s_mock_hvac_status.groups[0].temp_alarm = 28.0f;
    s_mock_hvac_status.groups[0].temp_bypass = 32.0f;

    nmbs_error err = adapter_modbus_tcp_on_read_holding_registers(0, 4, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read HR 0..3 failed");
    zassert_equal(regs[0], 200, "Tmin default mismatch: %u vs 200", regs[0]);
    zassert_equal(regs[1], 240, "Tmax default mismatch: %u vs 240", regs[1]);

    /* Write new setpoint to Group 0 (offset 0) */
    err = adapter_modbus_tcp_on_write_single_register(0, 215, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Write HR 0 failed");

    /* 4.2 Group Operational Mode and Interval (offset 40, 43) */
    err = adapter_modbus_tcp_on_write_single_register(40, (uint16_t)HVAC_MODE_AUTO, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Write mode failed");
    zassert_equal(s_mock_group0_mode, HVAC_MODE_AUTO, "Group 0 mode mismatch");

    err = adapter_modbus_tcp_on_write_single_register(43, 7, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Write rotation interval failed");
    zassert_equal(s_mock_group0_rotation_days, 7, "Rotation interval mismatch");

    /* 4.3 RTC Read/Write (offset 100..105) */
    regs[0] = 2026;
    regs[1] = 11;
    regs[2] = 12;
    regs[3] = 14;
    regs[4] = 30;
    regs[5] = 0;
    err = adapter_modbus_tcp_on_write_multiple_registers(100, 6, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Write RTC failed");
    zassert_equal(s_mock_rtc_datetime.year, 2026, "Year mismatch");
    zassert_equal(s_mock_rtc_datetime.month, 11, "Month mismatch");
    zassert_equal(s_mock_rtc_datetime.day, 12, "Day mismatch");
    zassert_equal(s_mock_rtc_datetime.hours, 14, "Hours mismatch");
    zassert_equal(s_mock_rtc_datetime.minutes, 30, "Minutes mismatch");
    zassert_equal(s_mock_rtc_datetime.seconds, 0, "Seconds mismatch");

    /* Read back RTC */
    memset(regs, 0, sizeof(regs));
    err = adapter_modbus_tcp_on_read_holding_registers(100, 6, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read back RTC failed");
    zassert_equal(regs[0], 2026, "Readback year mismatch");
    zassert_equal(regs[3], 14, "Readback hours mismatch");
}

/* ========================================================================= */
/* Test 5: Supervisory Command Execution                                     */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_supervisory_command_execution)
{
    uint16_t regs[5] = {0};

    /* 5.1 Acknowledge alarm ID 105 */
    s_last_acked_alarm = 0;
    regs[0] = MODBUS_SUPERVISORY_CMD_ACK_ALARM_ID;
    regs[1] = 105;
    regs[2] = 1; /* Trigger execute */
    nmbs_error err = adapter_modbus_tcp_on_write_multiple_registers(200, 3, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Supervisory Alarm Ack write failed");
    zassert_equal(s_last_acked_alarm, 105, "Alarm ack mismatch: %u vs 105", s_last_acked_alarm);

    /* Read supervisory status */
    err = adapter_modbus_tcp_on_read_holding_registers(200, 5, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Read supervisory status failed");
    zassert_equal(regs[2], 1, "Status should be 1 (Success)");
    zassert_equal(regs[3], MODBUS_SUPERVISORY_CMD_ACK_ALARM_ID, "Last opcode mismatch");
    zassert_equal(regs[4], 0, "Last errno should be 0");

    /* 5.2 Latch Reset */
    s_latch_reset_called = false;
    regs[0] = MODBUS_SUPERVISORY_CMD_RESET_LATCH;
    regs[1] = 0;
    regs[2] = 1;
    err = adapter_modbus_tcp_on_write_multiple_registers(200, 3, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Supervisory Latch Reset failed");
    zassert_true(s_latch_reset_called, "Latch reset should be called");

    /* 5.3 System Reboot */
    s_reboot_called = false;
    regs[0] = MODBUS_SUPERVISORY_CMD_REBOOT;
    regs[1] = 0xA55A; /* Confirmation key */
    regs[2] = 1;
    err = adapter_modbus_tcp_on_write_multiple_registers(200, 3, regs, 1, &s_adapter);
    zassert_equal(err, NMBS_ERROR_NONE, "Supervisory Reboot failed");
    zassert_true(s_reboot_called, "Reboot should be called with correct key");
}

/* ========================================================================= */
/* Test 6: Big-Endian 32-bit Packing / Unpacking                             */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_word_packing_endianness)
{
    /* Test float packing into 2 registers */
    float original_f = 230.5f;
    uint32_t raw_bits;
    memcpy(&raw_bits, &original_f, sizeof(raw_bits));

    uint16_t reg_hi = (uint16_t)((raw_bits >> 16) & 0xFFFF);
    uint16_t reg_lo = (uint16_t)(raw_bits & 0xFFFF);

    uint32_t unpacked_bits = ((uint32_t)reg_hi << 16) | (uint32_t)reg_lo;
    float unpacked_f;
    memcpy(&unpacked_f, &unpacked_bits, sizeof(unpacked_f));

    zassert_within(unpacked_f, original_f, 0.0001f, "Float unpack mismatch");

    /* Test uint32 packing into 2 registers */
    uint32_t original_u32 = 0x12345678U;
    reg_hi = (uint16_t)((original_u32 >> 16) & 0xFFFF);
    reg_lo = (uint16_t)(original_u32 & 0xFFFF);

    zassert_equal(reg_hi, 0x1234U, "High word mismatch");
    zassert_equal(reg_lo, 0x5678U, "Low word mismatch");

    uint32_t restored_u32 = ((uint32_t)reg_hi << 16) | (uint32_t)reg_lo;
    zassert_equal(restored_u32, original_u32, "Restored uint32 mismatch");
}

/* ========================================================================= */
/* Test 7: Active Object Coordinator & Port Binding Lifecycle                */
/* ========================================================================= */

ZTEST(modbus_tcp_mapping_suite, test_ao_modbus_tcp_lifecycle)
{
    static ao_modbus_tcp_t test_ao;
    const nmbs_callbacks *cbs = adapter_modbus_tcp_server_get_callbacks(&s_adapter);
    zassert_not_null(cbs, "Adapter callbacks should not be NULL");

    int err = ao_modbus_tcp_init(&test_ao, &s_config, cbs);
    zassert_equal(err, 0, "ao_modbus_tcp_init failed: %d", err);

    i_modbus_tcp_server_t port = ao_modbus_tcp_get_port(&test_ao);
    zassert_true(i_modbus_tcp_server_is_bound(&port), "Port should be bound");

    modbus_tcp_server_state_t state = MODBUS_TCP_STATE_UNINITIALIZED;
    err = i_modbus_tcp_server_get_state(&port, &state);
    zassert_equal(err, 0, "get_state failed");
    zassert_equal(state, MODBUS_TCP_STATE_STOPPED, "State should be STOPPED");

    zassert_false(i_modbus_tcp_server_is_running(&port), "Server should not be running yet");

    modbus_tcp_server_stats_t stats;
    err = i_modbus_tcp_server_get_stats(&port, &stats);
    zassert_equal(err, 0, "get_stats failed: %d", err);
    zassert_equal(stats.rx_frames, 0, "rx_frames should be 0");
    zassert_equal(stats.tx_frames, 0, "tx_frames should be 0");
    zassert_equal(stats.crc_errors, 0, "crc_errors should be 0");
    zassert_equal(stats.exceptions, 0, "exceptions should be 0");
    zassert_equal(stats.timeouts, 0, "timeouts should be 0");
}
