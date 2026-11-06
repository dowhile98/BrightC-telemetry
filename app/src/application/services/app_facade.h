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
#include "common/modbus_types.h"

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
typedef modbus_bus_cfg_t modbus_bus_config_t;

/* =========================================================================
 * Presentation View Data Structures (DTOs)
 * ========================================================================= */

/**
 * @brief Comprehensive HVAC subsystem runtime status view.
 */
typedef struct {
    uint8_t total_groups;
    uint8_t total_units;
    struct {
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
    } groups[HVAC_MAX_GROUPS];
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
 * @brief Register or update a remote Modbus slave device descriptor.
 *
 * Updates RAM registry and marks storage dirty for persistence to EEPROM.
 *
 * @param[in] slave Slave device descriptor.
 * @return 0 on success, negative errno on error.
 */
int app_facade_modbus_configure_slave(const modbus_slave_descriptor_t *slave);

/**
 * @brief Reconfigure electrical line parameters of a physical RS-485 bus.
 *
 * Reconfigures hardware transceiver on the wire, updates RAM registry,
 * and marks storage dirty for EEPROM persistence.
 *
 * @param[in] bus_id Physical bus ID (0 .. MODBUS_BUS_MAX - 1).
 * @param[in] cfg    Line configuration (baud rate, parity, stop bits).
 * @return 0 on success, negative errno on error.
 */
int app_facade_modbus_configure_bus(uint8_t bus_id, const modbus_bus_config_t *cfg);

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

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_SERVICES_APP_FACADE_H_ */
