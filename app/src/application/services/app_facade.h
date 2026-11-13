/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_facade.h
 * @brief Application Service Facade providing a unified, thread-safe API
 *        for presentation layers (Shell CLI, DWIN HMI, HTTP REST, Modbus TCP, SNMP).
 *
 * Adheres to Clean Architecture: Presentation components interact strictly
 * with this facade, isolating them from DI container internals, Active Objects,
 * and low-level peripheral adapters.
 */

#ifndef APP_APPLICATION_SERVICES_APP_FACADE_H_
#define APP_APPLICATION_SERVICES_APP_FACADE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "common/digital_input_types.h"
#include "common/hvac_types.h"
#include "common/indicator_types.h"
#include "common/alarm_types.h"
#include "common/modbus_types.h"
#include "common/modbus_tcp_types.h"
#include "common/network_types.h"
#include "common/sensor_types.h"
#include "common/thermal_energy_types.h"
#include "common/rtc_types.h"
#include "common/storage_fs_types.h"
#include "interfaces/i_storage_fs.h"
#include "application/zbus/sensor_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Enumerations and Configuration Types
 * ========================================================================= */

/**
 * @brief Operating modes selectable via presentation layers.
 */
typedef enum {
    HVAC_MODE_OFF = 0,       /**< All cooling/heating stages disabled */
    HVAC_MODE_COOL,          /**< Standard automatic cooling mode */
    HVAC_MODE_HEAT,          /**< Heating mode (where supported) */
    HVAC_MODE_AUTO,          /**< Automatic thermal coordination */
    HVAC_MODE_FAN_ONLY,      /**< Continuous ventilation purge only */
    HVAC_MODE_EMERGENCY      /**< Emergency safety shutdown / fire interlock */
} hvac_mode_t;

#define HVAC_GROUP_MODE_OFF        HVAC_MODE_OFF
#define HVAC_GROUP_MODE_COOL       HVAC_MODE_COOL
#define HVAC_GROUP_MODE_HEAT       HVAC_MODE_HEAT
#define HVAC_GROUP_MODE_AUTO       HVAC_MODE_AUTO
#define HVAC_GROUP_MODE_FAN_ONLY   HVAC_MODE_FAN_ONLY
#define HVAC_GROUP_MODE_EMERGENCY  HVAC_MODE_EMERGENCY
typedef hvac_mode_t hvac_group_mode_t;

/**
 * @brief Named operational profile configuration descriptor.
 */
typedef struct {
    hvac_operation_mode_t mode; /**< HVAC profile (@see hvac_operation_mode_t) */
    const char           *name; /**< Human-readable profile name */
} hvac_profile_config_t;

/**
 * @brief Physical Modbus bus configuration alias.
 */

/* =========================================================================
 * Presentation View Data Structures (DTOs)
 * ========================================================================= */

/**
 * @brief Comprehensive HVAC subsystem runtime status view.
 */
/**
 * @brief HVAC group status view structure.
 */
typedef struct {
    uint8_t            group_id;
    hvac_group_state_t state;
    uint32_t           bypass_causes_mask;
    float              highest_temp;
    float              temp_min;
    float              temp_max;
    float              temp_alarm;
    float              temp_bypass;
    uint8_t            active_turn_count;
    uint8_t            current_turn_offset;
    uint8_t            unit_mask;
    bool               is_manual;
    uint8_t            sensor_start;
    uint8_t            sensor_end;
    uint8_t            metric_policy;
    uint16_t           rotation_interval_days;
    uint32_t           rotation_timer_s;
    bool               enable_fan_delay;
    uint16_t           fan_delay_s;
} app_facade_hvac_group_view_t;

/**
 * @brief Comprehensive HVAC subsystem runtime status view.
 */
typedef struct {
    uint8_t total_groups;
    uint8_t total_units;
    app_facade_hvac_group_view_t groups[HVAC_MAX_GROUPS];
    struct {
        uint8_t           unit_id;
        hvac_unit_state_t state;
        hvac_tri_state_t  tri_state;
        bool              relay_aa;
        bool              relay_sm;
        bool              relay_g;
        bool              alarm_hp;
        bool              alarm_lp;
        bool              alarm_flow;
        bool              alarm_volt;
        uint8_t           fault_counter;
    } units[HVAC_MAX_UNITS_TOTAL];
} app_facade_hvac_status_t;

/**
 * @brief Temperature and sensor channels presentation view.
 */
typedef struct {
    float   internal_rtd_raw_counts[5];
    float   internal_rtd_ohms[5];
    float   internal_rtd_temps[5];
    bool    internal_rtd_valid[5];
    uint8_t internal_count;

    float   modbus_rtd_temps[16];
    bool    modbus_rtd_valid[16];
    uint8_t modbus_count;
} app_facade_sensor_view_t;

/**
 * @brief Digital input telemetry and contact alarms presentation view.
 */
typedef struct {
    uint8_t channel_count;
    struct {
        uint8_t   index;
        di_role_t role;
        const char *name;
        bool      raw_level;
        bool      is_active;
        uint32_t  active_duration_s;
    } channels[DI_CHANNEL_MAX];
    uint64_t raw_mask;
    uint64_t active_mask;
} app_facade_di_view_t;

/**
 * @brief Modbus master subsystem presentation view.
 */
typedef struct {
    struct {
        uint8_t  bus_id;
        uint32_t baudrate;
        uint8_t  parity;
        uint8_t  stop_bits;
        bool     is_online;
        size_t   slave_count;
    } buses[MODBUS_BUS_MAX];

    size_t total_slaves;
    struct {
        uint8_t               device_id;
        uint8_t               bus_id;
        uint8_t               slave_addr;
        modbus_device_type_t  dev_type;
        uint16_t              poll_interval_ms;
        uint16_t              timeout_ms;
        bool                  is_enabled;
        modbus_slave_health_t health;
        uint32_t              total_transactions;
        uint32_t              successful_transactions;
        uint32_t              timeouts;
        uint32_t              crc_errors;
        uint32_t              exceptions;
    } slaves[MODBUS_MAX_TOTAL_SLAVES];
} app_facade_modbus_view_t;

/**
 * @brief Non-volatile storage statistics presentation view.
 */
typedef struct {
    uint32_t write_count;
    uint32_t read_count;
    uint32_t crc_errors;
    uint32_t write_errors;
    uint32_t domains_persisted;
} storage_stats_t;

/**
 * @brief System diagnostics and hardware telemetry info.
 */
typedef struct {
    const char *firmware_version;
    const char *profile_name;
    uint64_t    uptime_seconds;
    size_t      free_heap_bytes;
    size_t      total_heap_bytes;
} app_facade_system_info_t;

/**
 * @brief Alarm subsystem summary status presentation view.
 */
typedef struct {
    size_t        active_count;
    size_t        total_registered;
    size_t        acknowledged_count;
    alarm_level_t highest_level;
    bool          buzzer_muted;
} app_facade_alarm_status_t;

/**
 * @brief Active alarm list item presentation view.
 */
typedef struct {
    uint16_t         alarm_id;
    alarm_level_t    level;
    alarm_state_t    state;
    alarm_category_t category;
    const char      *name;
    float            trigger_val;
    uint32_t         active_duration_ms;
    bool             acknowledged;
} app_facade_alarm_item_t;

/* =========================================================================
 * Lifecycle Management
 * ========================================================================= */

/**
 * @brief Bind application facade to the DI composition container.
 *
 * @param[in] container Pointer to initialized app_container_t.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_init(void *container);

/* =========================================================================
 * HVAC Control Service APIs
 * ========================================================================= */

/**
 * @brief Query complete status snapshot of HVAC groups and physical units.
 *
 * Thread-safe: synchronized against Active Object evaluation loops.
 *
 * @param[out] out_status Destination status structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_get_status(app_facade_hvac_status_t *out_status);

/**
 * @brief Set high-level operating mode for an HVAC group.
 *
 * @param[in] group_id Zero-indexed group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] mode     Target operating mode (@see hvac_mode_t).
 * @return 0 on success, negative errno on invalid arguments.
 */
int app_facade_hvac_set_mode(uint8_t group_id, hvac_mode_t mode);

/**
 * @brief Convenience alias for setting operating mode for an HVAC group.
 *
 * @param[in] group_id Zero-indexed group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] mode     Target operating mode (@see hvac_mode_t).
 * @return 0 on success, negative errno on invalid arguments.
 */
int app_facade_hvac_set_group_mode(uint8_t group_id, hvac_mode_t mode);

/**
 * @brief Set target temperature setpoints for an HVAC group.
 *
 * Automatically adjusts staged thresholds (hysteresis, boost alarm, critical bypass).
 *
 * @param[in] group_id          Zero-indexed group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] cool_target_x10   Target cooling setpoint in tenths of °C (e.g. 240 = 24.0°C).
 * @param[in] heat_target_x10   Target heating/low setpoint in tenths of °C (0 for auto hysteresis).
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_set_setpoint(uint8_t group_id, int16_t cool_target_x10, int16_t heat_target_x10);

/**
 * @brief Configure all four thermal threshold boundaries for an HVAC group.
 *
 * @param[in] group_id Group index (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] t_min    Low threshold (°C).
 * @param[in] t_max    Cooling setpoint (°C).
 * @param[in] t_alarm  High temperature warning threshold (°C).
 * @param[in] t_bypass Critical emergency bypass threshold (°C).
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_set_group_thermal_bounds(uint8_t group_id, float t_min, float t_max, float t_alarm, float t_bypass);

/**
 * @brief Force immediate unit duty rotation turn for an HVAC group.
 *
 * @param[in] group_id Zero-indexed group ID.
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_force_unit_rotation(uint8_t group_id);

/**
 * @brief Apply an operational profile (HVAC configuration + Digital Input mappings).
 *
 * Updates RAM, actuator routing, and schedules persistence to EEPROM.
 *
 * @param[in] profile Profile descriptor to apply.
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_apply_profile(const hvac_profile_config_t *profile);

/**
 * @brief Manually override compressor or fan state for an individual AC unit.
 *
 * @param[in] unit_id        Zero-indexed AC unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[in] enable         true to activate manual override, false to restore automatic control.
 * @param[in] run_compressor true to command compressor contactor ON.
 * @param[in] run_fan        true to command fan purge relay ON.
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_set_unit_manual(uint8_t unit_id, bool enable, bool run_compressor, bool run_fan);

/**
 * @brief Configure sensor probe range and aggregation metric policy for an HVAC group.
 *
 * @param[in] group_id     Zero-indexed group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] sensor_start 1-based start sensor ID (e.g. 1 for ID 1), or 0 if unmapped.
 * @param[in] sensor_end   1-based end sensor ID (e.g. 4 for ID 4), or 0 if unmapped.
 * @param[in] policy       Metric policy (@see sensor_metric_policy_t: MAX, AVG, MIN).
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_configure_group_sensors(uint8_t group_id,
                                            uint8_t sensor_start,
                                            uint8_t sensor_end,
                                            sensor_metric_policy_t policy);

/**
 * @brief Configure AC unit mask and active turn count (N+M duty rotation) for an HVAC group.
 *
 * @param[in] group_id          Zero-indexed group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] active_turn_count Desired concurrent running units (N).
 * @param[in] unit_mask         Bitmask of assigned physical units (0 to retain existing mask).
 * @return 0 on success, negative errno on error.
 */
int app_facade_hvac_configure_group_units(uint8_t group_id,
                                          uint8_t active_turn_count,
                                          uint8_t unit_mask);

/**
 * @brief Configure duty rotation interval in days for an HVAC group.
 *
 * @param[in] group_id Zero-indexed HVAC group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] days     Rotation interval in days (range 1..365; typical: 7, 15, 20, 30).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_set_group_rotation(uint8_t group_id, uint16_t days);

/**
 * @brief Configure Relay G fan control (enable_fan_delay) and post-purge delay for an HVAC group.
 *
 * When enable is false (default), Relay G remains de-energized (0 = NC closed, fan always enabled).
 * When enable is true, Relay G energizes (1 = NC open, fan OFF) when the AC unit is idle/stopped
 * after the post-purge delay (fan_delay_s).
 *
 * @param[in] group_id     Zero-indexed HVAC group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in] enable       true to turn off fan when AC is idle, false to keep fan always enabled.
 * @param[in] fan_delay_s  Post-cooling fan purge delay in seconds (0 to retain current value).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_set_group_fan_control(uint8_t group_id, bool enable, uint16_t fan_delay_s);

/**
 * @brief Inject a temporary test temperature across all HVAC groups.
 *
 * @param[in] temp Injected temperature in °C.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_set_test_temp(float temp);

/**
 * @brief Clear injected test temperature override and resume live sensor readings.
 *
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_clear_test_temp(void);

/**
 * @brief Directly actuate a physical internal relay channel for hardware testing.
 *
 * @param[in] ch    Relay channel (0..17).
 * @param[in] state true for CLOSED (active/ON), false for OPEN (inactive/OFF).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_test_relay(uint8_t ch, bool state);

/**
 * @brief Reset HVAC domain in EEPROM storage to baseline defaults (26/28/30/32 setpoints) and apply to RAM.
 *
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_reset_eeprom(void);

/**
 * @brief Query active countdown and interval timers for an HVAC group and units.
 *
 * @param[in]  group_idx        HVAC group index (0..HVAC_MAX_GROUPS-1).
 * @param[out] cooling_timer_s  Accumulated cooling duration in seconds (may be NULL).
 * @param[out] rotation_timer_s Elapsed rotation interval in seconds (may be NULL).
 * @param[out] startup_timers   Array of size HVAC_MAX_UNITS_TOTAL for unit startup countdowns (may be NULL).
 * @param[out] fan_timers       Array of size HVAC_MAX_UNITS_TOTAL for fan purge countdowns (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_hvac_get_timers(uint8_t group_idx,
                               uint32_t *cooling_timer_s,
                               uint32_t *rotation_timer_s,
                               uint16_t *startup_timers,
                               uint16_t *fan_timers);

/* =========================================================================
 * Sensor Matrix Service APIs
 * ========================================================================= */

/**
 * @brief Query current readings across internal and external Modbus RTD sensors.
 *
 * @param[out] out_view Destination sensor view structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_sensor_get_readings(app_facade_sensor_view_t *out_view);

/**
 * @brief Retrieve latest raw physical sensor readings bank.
 *
 * @param[out] out_raw Destination raw readings structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_sensor_get_raw(raw_sensor_readings_t *out_raw);

/**
 * @brief Query current semantic thermal matrix frame from the sensor subsystem.
 *
 * @param[out] out_frame Destination thermal matrix frame structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_sensor_get_matrix(thermal_matrix_frame_t *out_frame);

/**
 * @brief Query current flat sensor telemetry snapshot from Zbus chan_sensor_snapshot.
 *
 * @param[out] out_snapshot Destination snapshot message structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_sensor_get_snapshot(sensor_snapshot_msg_t *out_snapshot);

/**
 * @brief Update calibration offset for a sensor channel.
 *
 * @param[in] sensor_idx  Physical channel index.
 * @param[in] offset_x10  Calibration offset in tenths of °C (e.g. -5 = -0.5°C).
 * @return 0 on success, negative errno on error.
 */
int app_facade_sensor_set_calibration(uint8_t sensor_idx, int16_t offset_x10);

/* =========================================================================
 * Digital Input & Telemetry Service APIs
 * ========================================================================= */

/**
 * @brief Query current status and timing of all configured digital input channels.
 *
 * @param[out] out_view Destination digital input view structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_di_get_status(app_facade_di_view_t *out_view);

/**
 * @brief Retrieve raw 64-bit hardware input bitmask.
 *
 * @param[out] out_raw_mask 64-bit mask of raw pin levels.
 * @return 0 on success, negative errno on error.
 */
int app_facade_di_get_raw(uint64_t *out_raw_mask);

/**
 * @brief Query current consolidated digital input telemetry snapshot from Zbus.
 *
 * @param[out] out_snapshot Destination snapshot message structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_di_get_snapshot(di_snapshot_msg_t *out_snapshot);

/**
 * @brief Convert a digital input semantic role into human-readable signal name.
 *
 * @param[in] role Semantic role enum.
 * @return String description or "Unknown Role".
 */
const char *app_facade_di_role_name(di_role_t role);

/* =========================================================================
 * Modbus Master Service APIs
 * ========================================================================= */

/**
 * @brief Query bus configurations, health, and slave statistics.
 *
 * @param[out] out_view Destination modbus view structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_modbus_get_status(app_facade_modbus_view_t *out_view);

/**
 * @brief Retrieve diagnostic metrics from the Modbus TCP Server.
 *
 * @param[out] out_stats Destination buffer for server metrics.
 * @return 0 on success, negative errno on error.
 */
int app_facade_modbus_tcp_get_stats(modbus_tcp_server_stats_t *out_stats);

/**
 * @brief Retrieve descriptor of a registered Modbus slave device.
 *
 * Read-only: the topology is compile-time, so there is no counterpart that
 * registers, moves or reconfigures a slave at runtime.
 *
 * @param[in]  device_id Device identifier.
 * @param[out] out_desc  Destination descriptor pointer.
 * @return 0 on success, negative errno on error.
 */
int app_facade_modbus_get_slave(uint8_t device_id, modbus_slave_descriptor_t *out_desc);

/* =========================================================================
 * Storage / Persistence Service APIs
 * ========================================================================= */

/**
 * @brief Synchronously commit all dirty configuration domains to EEPROM.
 *
 * @return Number of domains persisted (>= 0), or negative errno on error.
 */
int app_facade_storage_save_all(void);

/**
 * @brief Reload all configuration domains from non-volatile EEPROM storage.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_storage_reload_all(void);

/**
 * @brief Reset EEPROM non-volatile storage slots to factory defaults.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_storage_reset_defaults(void);

/**
 * @brief Retrieve storage engine transaction and error statistics.
 *
 * @param[out] out_stats Destination statistics structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_storage_get_stats(storage_stats_t *out_stats);

/* =========================================================================
 * System / Diagnostics Service APIs
 * ========================================================================= */

/**
 * @brief Retrieve firmware version, profile name, uptime, and heap metrics.
 *
 * @param[out] out_info Destination system info structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_system_get_info(app_facade_system_info_t *out_info);

/**
 * @brief Trigger a system software reset (reboot).
 *
 * @return Does not return on success; negative errno on failure.
 */
int app_facade_system_reboot(void);

/* =========================================================================
 * Alarm Management Service APIs
 * ========================================================================= */

/**
 * @brief Retrieve overall alarm subsystem summary status.
 *
 * @param[out] out_status Destination status structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_alarm_get_status(app_facade_alarm_status_t *out_status);

/**
 * @brief Retrieve list of currently active or acknowledged alarms.
 *
 * @param[out] out_items Destination buffer for alarm items.
 * @param[in]  max_items Maximum entries to write to buffer.
 * @return Number of items written.
 */
size_t app_facade_alarm_get_active_list(app_facade_alarm_item_t *out_items, size_t max_items);

/**
 * @brief Retrieve catalog of registered alarm descriptors.
 *
 * @param[out] out_catalog Destination array of descriptor pointers.
 * @param[in]  max_items   Maximum entries to populate.
 * @return Number of pointers written.
 */
size_t app_facade_alarm_get_catalog(const alarm_descriptor_t **out_catalog, size_t max_items);

/**
 * @brief Retrieve live snapshot and static descriptor for a specific alarm ID.
 *
 * @param[in]  alarm_id     Target alarm identifier.
 * @param[out] out_snapshot Destination snapshot structure.
 * @param[out] out_desc     Optional pointer to receive descriptor reference (can be NULL).
 * @return 0 on success, negative errno on error.
 */
int app_facade_alarm_get_snapshot(uint16_t alarm_id, alarm_snapshot_entry_t *out_snapshot, const alarm_descriptor_t **out_desc);

/**
 * @brief Acknowledge an active alarm condition by ID.
 *
 * @param[in] alarm_id Target alarm identifier.
 * @return 0 on success, negative errno on error.
 */
int app_facade_alarm_ack(uint16_t alarm_id);

/**
 * @brief Reset a latched alarm condition by ID (0 to reset all latched alarms).
 *
 * @param[in] alarm_id Target alarm identifier (0 for all).
 * @return 0 on success, negative errno on error.
 */
int app_facade_alarm_reset_latch(uint16_t alarm_id);

/**
 * @brief Set or clear persistent alarm buzzer mute status.
 *
 * @param[in] muted true to mute alarm acoustic patterns, false for audible.
 * @return 0 on success, negative errno on error.
 */
int app_facade_alarm_set_buzzer_mute(bool muted);

/**
 * @brief Check whether alarm buzzer is currently muted.
 *
 * @return true if muted, false otherwise.
 */
bool app_facade_alarm_is_buzzer_muted(void);

/* =========================================================================
 * Thermal and Energy Subsystem Presentation APIs
 * ========================================================================= */

/**
 * @brief Retrieve latest thermodynamic calculation snapshot.
 *
 * @param[out] out_result Destination result structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_get_result(thermal_energy_result_t *out_result);

/**
 * @brief Retrieve latest raw multi-source telemetry capture snapshot.
 *
 * @param[out] out_capture Destination capture structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_get_capture(thermal_energy_capture_t *out_capture);

/**
 * @brief Retrieve a compressed hourly history record from the 720h ring buffer.
 *
 * @param[in]  hours_ago  History offset (0 = most recent closed hour, up to 719).
 * @param[out] out_record Destination hourly record structure.
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_get_hourly_history(uint16_t hours_ago, thermal_hourly_record_t *out_record);

/**
 * @brief Retrieve the active learned steady-state baseline parameters.
 *
 * @param[out] out_baseline Destination baseline configuration.
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_get_baseline(config_thermal_baseline_t *out_baseline);

/**
 * @brief Retrieve active site thermal topology configuration from RAM.
 *
 * @param[out] out_site_cfg Destination site configuration.
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_get_site_config(config_thermal_site_t *out_site_cfg);

/**
 * @brief Trigger recalibration and re-training of the steady-state baseline.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_thermal_trigger_recalibration(void);

/* =========================================================================
 * Real-Time Clock & System Time Service APIs
 * ========================================================================= */

/**
 * @brief Obtain current broken-down calendar date and time.
 *
 * @param[out] out_dt Destination date/time structure.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_get_datetime(rtc_date_time_t *out_dt);

/**
 * @brief Commit calendar date and time adjustment from an authorized source.
 *
 * @param[in] dt     New calendar date and time.
 * @param[in] source Originating authority.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_set_datetime(const rtc_date_time_t *dt, rtc_time_source_t source);

/**
 * @brief Obtain current UTC Unix epoch seconds and optional subsecond milliseconds.
 *
 * @param[out] out_epoch_s       Pointer to receive UTC epoch seconds.
 * @param[out] out_subsecond_ms  Optional pointer to receive subsecond milliseconds (0..999, may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_get_unix(int64_t *out_epoch_s, uint16_t *out_subsecond_ms);

/**
 * @brief Fast query alias: obtain current UTC Unix epoch time and subsecond milliseconds.
 */
int app_facade_time_get_epoch(int64_t *out_sec, uint32_t *out_ms);

/**
 * @brief Commit current time from UTC Unix epoch seconds and authority source.
 *
 * @param[in] epoch_s UTC epoch seconds.
 * @param[in] source  Originating authority.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_set_unix(int64_t epoch_s, rtc_time_source_t source);

/**
 * @brief Format current UTC timestamp into an ISO-8601 string ("YYYY-MM-DDTHH:MM:SS.mmmZ").
 *
 * @param[out] buf    Destination character buffer.
 * @param[in]  buf_sz Capacity of buffer (must be >= RTC_ISO8601_STR_LEN).
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_get_iso8601(char *buf, size_t buf_sz);

/**
 * @brief Format current UTC timestamp alias.
 */
int app_facade_time_format_iso8601(char *buf, size_t buf_sz);

/**
 * @brief Query current synchronization state and diagnostic metadata.
 *
 * @param[out] out_status Pointer to status structure to populate.
 * @return 0 on success, negative errno on failure.
 */
int app_facade_time_get_status(rtc_sync_status_t *out_status);

/**
 * @brief Obtain current 32-bit DOS FATFS timestamp for MicroSD file writes.
 *
 * @return Packed 32-bit DOS timestamp.
 */
uint32_t app_facade_time_get_fattime(void);

/* =========================================================================
 * Network Service APIs
 * ========================================================================= */

/**
 * @brief Get runtime status of a network interface.
 *
 * @param[in]  iface      Interface to query (Ethernet or WiFi).
 * @param[out] out_status Destination status structure.
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_get_status(network_interface_t iface, net_status_t *out_status);

/**
 * @brief Get the active Ethernet configuration from the network Active Object.
 *
 * @param[out] out_cfg Destination configuration structure.
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_get_eth_config(net_eth_config_t *out_cfg);

/**
 * @brief Get the active WiFi configuration from the network Active Object.
 *
 * @param[out] out_cfg Destination configuration structure.
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_get_wifi_config(net_wifi_config_t *out_cfg);

/**
 * @brief Set Ethernet interface to DHCP mode.
 *
 * Reconfigures Ethernet to dynamic IP allocation, posts config update
 * to ao_network, and marks CONFIG_DOMAIN_NET_ETH dirty in storage.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_network_eth_set_dhcp(void);

/**
 * @brief Set Ethernet interface to static IPv4 parameters.
 *
 * @param[in] ip      IPv4 host address in host-byte order (e.g., 0xC0A80132 for 192.168.1.50).
 * @param[in] netmask Subnet mask in host-byte order (e.g., 0xFFFFFF00 for 255.255.255.0).
 * @param[in] gateway Default gateway in host-byte order.
 * @return 0 on success, -EINVAL on invalid IP configuration, negative errno on failure.
 */
int app_facade_network_eth_set_static(uint32_t ip, uint32_t netmask, uint32_t gateway);

/**
 * @brief Trigger a WiFi access-point scan.
 *
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_wifi_scan(void);

/**
 * @brief Configure and connect WiFi station to an access point.
 *
 * Preserves Soft-AP mode (promoting to NET_WIFI_MODE_STA_AP if active),
 * sets DHCP client for STA, posts event to ao_network, initiates
 * connection attempt, and marks CONFIG_DOMAIN_NET_WIFI dirty.
 *
 * @param[in] ssid SSID string (null-terminated, non-NULL).
 * @param[in] psk  Pre-shared key (null-terminated, required unless sec is OPEN).
 * @param[in] sec  Security protocol.
 * @return 0 on success, negative errno on error.
 */
int app_facade_network_wifi_connect(const char *ssid, const char *psk, net_wifi_security_t sec);

/**
 * @brief Disconnect WiFi station from the current access point.
 *
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_wifi_disconnect(void);

/**
 * @brief Force immediate write/flush of dirty network configurations to persistent storage (EEPROM).
 *
 * @return 0 on success, -EINVAL or -ENODEV on error.
 */
int app_facade_network_save(void);

/**
 * @brief Compatibility alias for app_facade_network_save.
 */
int app_facade_network_wifi_config_save(void);

/* =========================================================================
 * MicroSD Storage & Filesystem Service APIs
 * ========================================================================= */

/**
 * @brief Retrieve MicroSD card presence, mount state, and volume capacity.
 *
 * @param[out] out_status Destination status pointer (may be NULL).
 * @param[out] out_vol    Destination volume info pointer (may be NULL).
 * @return 0 on success, negative errno on error.
 */
int app_facade_sd_get_status(storage_fs_card_status_t *out_status, storage_fs_volume_info_t *out_vol);

/**
 * @brief Request manual mount of the MicroSD card.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_sd_mount(void);

/**
 * @brief Request clean unmount / safe ejection of the MicroSD card.
 *
 * @return 0 on success, negative errno on error.
 */
int app_facade_sd_eject(void);

/**
 * @brief Get the abstract MicroSD storage filesystem port handle.
 *
 * @return Pointer to storage fs port, or NULL if uninitialized.
 */
const i_storage_fs_t *app_facade_get_sd_storage(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_SERVICES_APP_FACADE_H_ */
