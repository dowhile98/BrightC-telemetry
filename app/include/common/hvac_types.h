/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, hardware routing structures, and configuration
 * definitions for the Industrial HVAC Control Subsystem.
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_COMMON_HVAC_TYPES_H_
#define APP_COMMON_HVAC_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Dimensional Limits                                                        */
/* ========================================================================= */

/** Maximum number of air conditioning units manageable by the firmware. */
#define HVAC_MAX_UNITS_TOTAL        8U

/** Maximum number of thermal coordination groups. */
#define HVAC_MAX_GROUPS             4U

/** Maximum number of temperature sensor probes assignable per group. */
#define HVAC_MAX_SENSORS_PER_GROUP  16U

/* ========================================================================= */
/* Enumerations                                                              */
/* ========================================================================= */

/**
 * @brief System operation mode profiles.
 */
typedef enum {
    /** Passive monitoring & telemetry only; relays disengaged (relay_sm = 0). */
    HVAC_MODE_TELEMETRY_ONLY     = 0,

    /** Autonomous HVAC sequencing controller without psychrometric telemetry. */
    HVAC_MODE_HVAC_CONTROL_ONLY = 1,

    /** Full hybrid closed-loop actuation and predictive telemetry analysis. */
    HVAC_MODE_HYBRID_FULL        = 2,
} hvac_operation_mode_t;

/**
 * @brief Physical hardware I/O backend for actuators and alarm contacts.
 */
typedef enum {
    HVAC_IO_BACKEND_NONE          = 0, /**< Unmapped / disabled channel */
    HVAC_IO_BACKEND_LOCAL_GPIO    = 1, /**< Direct on-chip STM32 GPIO */
    HVAC_IO_BACKEND_LOCAL_MCP23S17 = 2, /**< On-board SPI GPIO expander (MCP23S17) */
    HVAC_IO_BACKEND_REMOTE_MODBUS = 3, /**< Remote RS-485 Modbus RTU slave */
} hvac_io_backend_t;

/**
 * @brief Tri-state actuator command level.
 */
typedef enum {
    HVAC_TRI_STATE_OFF    = 0, /**< Both compressor and bypass relays open (OFF) */
    HVAC_TRI_STATE_ON     = 1, /**< Smart mode compressor active (relay_aa=1, relay_sm=1) */
    HVAC_TRI_STATE_BYPASS = 2, /**< Transferred to mechanical thermostat (relay_sm=0) */
} hvac_tri_state_t;

/**
 * @brief Individual AC unit finite state machine states.
 */
typedef enum {
    UNIT_STATE_OFF = 0,      /**< Idle, compressor off */
    UNIT_STATE_STARTING,     /**< In anti-inrush staggered delay window */
    UNIT_STATE_RUNNING,      /**< Actively cooling (compressor engaged) */
    UNIT_STATE_STOPPING,     /**< Shutdown purge / fan post-delay running */
    UNIT_STATE_BLOCKED_ALARM,/**< Lockout timer active after pressure/electrical fault */
    UNIT_STATE_FAULT,        /**< Permanently disqualified from duty rotation */
    UNIT_STATE_BYPASS,       /**< Transferred to mechanical bypass */
} hvac_unit_state_t;

/**
 * @brief Group coordination finite state machine states.
 */
typedef enum {
    GROUP_STATE_IDLE = 0,    /**< Room temperature at or below temp_min */
    GROUP_STATE_COOLING,     /**< Normal cooling with N active units */
    GROUP_STATE_COOL_BOOST,  /**< Temperature >= temp_alarm; all N+M units energized */
    GROUP_STATE_BYPASS,      /**< Mechanical bypass active (relay_sm = 0) */
    GROUP_STATE_FIRE,        /**< Fire alarm active; all units and fans shut down */
} hvac_group_state_t;

/**
 * @brief Multi-cause bypass flags bitmask.
 */
typedef enum {
    BYPASS_CAUSE_NONE            = 0,
    BYPASS_CAUSE_PHYSICAL_SWITCH = (1U << 0), /**< Front-panel maintenance toggle switch */
    BYPASS_CAUSE_HIGH_TEMP       = (1U << 1), /**< Temperature exceeded temp_bypass */
    BYPASS_CAUSE_COOLING_TIMEOUT = (1U << 2), /**< Room failed two successive cooling cycles */
    BYPASS_CAUSE_ALL_FAULTS      = (1U << 3), /**< All units in group reached max fault limit */
    BYPASS_CAUSE_SENSOR_FAULT    = (1U << 4), /**< Zero healthy sensors reporting in group */
    BYPASS_CAUSE_REMOTE_COMMAND  = (1U << 5), /**< Forced bypass via HMI/Modbus/SNMP */
    BYPASS_CAUSE_DISCREPANCY     = (1U << 6), /**< Contactor feedback discrepancy detected */
} hvac_bypass_cause_t;

/* ========================================================================= */
/* Structures                                                                */
/* ========================================================================= */

/**
 * @brief Hardware I/O endpoint mapping descriptor (4 bytes).
 */
typedef struct {
    uint8_t  backend;   /**< Backend type (@see hvac_io_backend_t) */
    uint8_t  device_id; /**< Expander index (0/1) or Modbus Slave Address */
    uint16_t channel;   /**< GPIO pin, expander pin (0..15), or Modbus coil/reg */
} hvac_io_endpoint_t;

/**
 * @brief Hardware routing configuration for a single AC unit (36 bytes).
 */
typedef struct {
    /* Actuation Endpoints (Outputs) */
    hvac_io_endpoint_t relay_aa; /**< Compressor contactor: Smart Mode ON/OFF */
    hvac_io_endpoint_t relay_sm; /**< Mode selector: Smart Controller (1) / Bypass (0) */
    hvac_io_endpoint_t relay_g;  /**< Fan purge relay: Forced ventilation ON/OFF */

    /* Alarm Input Endpoints */
    hvac_io_endpoint_t alarm_hp;   /**< High-pressure switch alarm input */
    hvac_io_endpoint_t alarm_lp;   /**< Low-pressure switch alarm input */
    hvac_io_endpoint_t alarm_flow; /**< Airflow / water supply fault input */
    hvac_io_endpoint_t alarm_volt; /**< Voltage / phase monitor fault input */

    /* Feedback Input Endpoints */
    hvac_io_endpoint_t fb_compressor; /**< Auxiliary contact feedback from compressor */
    hvac_io_endpoint_t fb_fan;        /**< Auxiliary contact feedback from fan */
} hvac_unit_hw_config_t;

/**
 * @brief Persistent configuration parameters for an HVAC group (38 bytes payload, 40 bytes aligned).
 */
typedef struct {
    /* Temperature Setpoints (Celsius) */
    float temp_min;    /**< Target low setpoint: cooling stops when T <= temp_min */
    float temp_max;    /**< Target high setpoint: cooling starts when T >= temp_max */
    float temp_alarm;  /**< High temp boost setpoint: all units start when T >= temp_alarm */
    float temp_bypass; /**< Critical bypass setpoint: bypass trips when T >= temp_bypass */

    /* Timers (seconds) */
    uint16_t time_to_cool_s;    /**< Maximum duration permitted to bring room to temp_min */
    uint16_t delay_on_s;        /**< Staggered anti-inrush delay between starting units */
    uint16_t fan_delay_s;       /**< Fan purge run duration after compressor stops */
    uint16_t pressure_retry_s;  /**< Lockout retry delay after transient alarm (default 180s) */
    uint16_t manual_timeout_s;  /**< Automatic reversion timer for manual overrides */

    /* Rotation & Generic N+M Parameters */
    uint16_t rotation_interval_hours; /**< Hours between duty shift rotations */
    uint16_t sensor_mask;             /**< Bitmask of assigned temperature probes (0..15) */
    uint8_t  unit_mask;               /**< Bitmask of assigned AC units (0..7) */
    uint8_t  active_turn_count;       /**< Number of units actively running in turn (N) */

    /* Limits & Flags */
    uint8_t max_allowed_faults; /**< Maximum fault lockouts before permanent unit eviction */
    uint8_t enable_fan_delay;   /**< 1 to enable fan purge cycle, 0 to disable */
    uint8_t hum_min;            /**< Minimum relative humidity threshold (%) */
    uint8_t hum_max;            /**< Maximum relative humidity threshold (%) */
    uint8_t reserved[2];        /**< Reserved alignment padding */
} config_hvac_group_t;

/**
 * @brief Root persistent HVAC storage configuration structure (~460-476 bytes).
 */
typedef struct {
    uint8_t               operation_mode; /**< Operation profile (@see hvac_operation_mode_t) */
    uint8_t               total_units;    /**< Total number of configured units (1..8) */
    uint8_t               total_groups;   /**< Total number of configured groups (1..4) */
    uint8_t               reserved1;      /**< Alignment padding */
    hvac_io_endpoint_t    fire_interlock; /**< Emergency fire shutdown input endpoint */
    hvac_io_endpoint_t    bypass_switch;  /**< Front-panel physical bypass switch endpoint */
    config_hvac_group_t   groups[HVAC_MAX_GROUPS];     /**< Group configuration array */
    hvac_unit_hw_config_t units[HVAC_MAX_UNITS_TOTAL]; /**< Hardware unit routing array */
    uint8_t               reserved[16];   /**< Future expansion padding */
} config_hvac_t;

/* ========================================================================= */
/* Pure Validation Functions                                                 */
/* ========================================================================= */

/**
 * @brief Validate operation mode enumerator.
 *
 * @param mode Mode to validate.
 * @return true if valid, false otherwise.
 */
static inline bool hvac_operation_mode_is_valid(uint8_t mode)
{
    return (mode <= (uint8_t)HVAC_MODE_HYBRID_FULL);
}

/**
 * @brief Validate I/O backend enumerator.
 *
 * @param backend Backend to validate.
 * @return true if valid, false otherwise.
 */
static inline bool hvac_io_backend_is_valid(uint8_t backend)
{
    return (backend <= (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS);
}

/**
 * @brief Validate an HVAC I/O endpoint descriptor.
 *
 * Checks that the backend is valid, and enforces channel limits based on backend type:
 * - NONE: valid, channel ignored.
 * - LOCAL_GPIO / LOCAL_MCP23S17: channel must be < 32.
 * - REMOTE_MODBUS: channel must be < 32.
 *
 * @param ep Endpoint to validate.
 * @return true if valid, false otherwise.
 */
static inline bool hvac_endpoint_is_valid(const hvac_io_endpoint_t *ep)
{
    if (ep == NULL) {
        return false;
    }

    if (!hvac_io_backend_is_valid(ep->backend)) {
        return false;
    }

    if (ep->backend == (uint8_t)HVAC_IO_BACKEND_NONE) {
        return true;
    }

    /* Channel must be within valid channel range (0..31) */
    return (ep->channel < 32U);
}

/**
 * @brief Validate tri-state enumerator.
 *
 * @param tri Tri-state value to validate.
 * @return true if valid, false otherwise.
 */
static inline bool hvac_tri_state_is_valid(uint8_t tri)
{
    return (tri <= (uint8_t)HVAC_TRI_STATE_BYPASS);
}

/**
 * @brief Validate configuration parameters of an HVAC group.
 *
 * Checks setpoint ordering (temp_min <= temp_max <= temp_alarm <= temp_bypass),
 * humidity range (hum_min <= hum_max <= 100%), and active unit count limit.
 *
 * @param group Group configuration to check (may be NULL).
 * @return true if group is internally valid, false otherwise.
 */
static inline bool config_hvac_group_is_valid(const config_hvac_group_t *group)
{
    if (group == NULL) {
        return false;
    }

    if (group->temp_min > group->temp_max ||
        group->temp_max > group->temp_alarm ||
        group->temp_alarm > group->temp_bypass) {
        return false;
    }

    if (group->hum_min > group->hum_max || group->hum_max > 100U) {
        return false;
    }

    if (group->active_turn_count > HVAC_MAX_UNITS_TOTAL) {
        return false;
    }

    return true;
}

/**
 * @brief Validate root HVAC configuration structure.
 *
 * Checks operation mode, unit and group counts, each configured group,
 * endpoint validity across units and interlocks, and coherence between
 * group unit_masks and total_units.
 *
 * @param cfg HVAC configuration to check (may be NULL).
 * @return true if valid, false otherwise.
 */
static inline bool config_hvac_is_valid(const config_hvac_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }

    if (!hvac_operation_mode_is_valid(cfg->operation_mode)) {
        return false;
    }

    if (cfg->total_units > HVAC_MAX_UNITS_TOTAL) {
        return false;
    }

    if (cfg->total_groups > HVAC_MAX_GROUPS) {
        return false;
    }

    if (!hvac_endpoint_is_valid(&cfg->fire_interlock) ||
        !hvac_endpoint_is_valid(&cfg->bypass_switch)) {
        return false;
    }

    uint8_t allowed_unit_mask = 0U;
    if (cfg->total_units == 8U) {
        allowed_unit_mask = 0xFFU;
    } else if (cfg->total_units > 0U) {
        allowed_unit_mask = (uint8_t)((1U << cfg->total_units) - 1U);
    }

    for (uint8_t i = 0U; i < cfg->total_groups; i++) {
        if (!config_hvac_group_is_valid(&cfg->groups[i])) {
            return false;
        }

        if ((cfg->groups[i].unit_mask & ~allowed_unit_mask) != 0U) {
            return false;
        }

        uint8_t assigned_units = 0U;
        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            if ((cfg->groups[i].unit_mask & (1U << u)) != 0U) {
                assigned_units++;
            }
        }
        if (cfg->groups[i].active_turn_count > assigned_units) {
            return false;
        }
    }

    for (uint8_t u = 0U; u < cfg->total_units; u++) {
        const hvac_unit_hw_config_t *unit = &cfg->units[u];
        if (!hvac_endpoint_is_valid(&unit->relay_aa) ||
            !hvac_endpoint_is_valid(&unit->relay_sm) ||
            !hvac_endpoint_is_valid(&unit->relay_g) ||
            !hvac_endpoint_is_valid(&unit->alarm_hp) ||
            !hvac_endpoint_is_valid(&unit->alarm_lp) ||
            !hvac_endpoint_is_valid(&unit->alarm_flow) ||
            !hvac_endpoint_is_valid(&unit->alarm_volt) ||
            !hvac_endpoint_is_valid(&unit->fb_fan) ||
            !hvac_endpoint_is_valid(&unit->fb_compressor)) {
            return false;
        }
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_HVAC_TYPES_H_ */
