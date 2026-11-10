/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_types.h
 * @brief Pure domain types and data contracts for the Alarm Management Subsystem.
 *
 * ISO C99 only. Zero Zephyr RTOS or vendor hardware dependencies.
 */

#ifndef APP_COMMON_ALARM_TYPES_H_
#define APP_COMMON_ALARM_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of simultaneous alarm descriptors supported by default.
 */
#ifndef CONFIG_ALARM_MANAGER_MAX_ALARMS
#define CONFIG_ALARM_MANAGER_MAX_ALARMS 128
#endif

/**
 * @brief Canonical alarm operational states.
 */
typedef enum {
    ALARM_STATE_INACTIVE = 0,     /**< Normal condition; alarm quiescent */
    ALARM_STATE_ACTIVE,           /**< Anomaly condition asserted */
    ALARM_STATE_ACKNOWLEDGED,     /**< Operator acknowledged active alarm */
    ALARM_STATE_LATCHED,          /**< Condition cleared while latching active */
    ALARM_STATE_MAX               /**< Sentinel value */
} alarm_state_t;

/**
 * @brief Alarm severity levels for arbitration and signaling.
 */
typedef enum {
    ALARM_SEVERITY_INFO = 0,      /**< Informational notification */
    ALARM_SEVERITY_WARNING,       /**< Warning: non-fatal anomaly */
    ALARM_SEVERITY_CRITICAL,      /**< Critical: equipment service or safety trip */
    ALARM_SEVERITY_FATAL,         /**< Fatal: emergency facility danger (e.g. Fire) */
    ALARM_SEVERITY_MAX            /**< Sentinel value */
} alarm_severity_t;

/**
 * @brief Alarm priority levels for escalation and dispatch ordering.
 */
typedef enum {
    ALARM_PRIORITY_LOW = 0,       /**< Low priority */
    ALARM_PRIORITY_MEDIUM,        /**< Medium priority */
    ALARM_PRIORITY_HIGH,          /**< High priority */
    ALARM_PRIORITY_EMERGENCY,     /**< Highest / Emergency priority */
    ALARM_PRIORITY_MAX            /**< Sentinel value */
} alarm_priority_t;

/**
 * @brief Source category of the alarm condition.
 */
typedef enum {
    ALARM_SOURCE_DIGITAL_INPUT = 0, /**< Electromechanical contact / switch */
    ALARM_SOURCE_SENSOR_ANALOG,     /**< Temperature probe / analog transducer */
    ALARM_SOURCE_ELECTRICAL,        /**< 3-Phase power quality / AC line voltage */
    ALARM_SOURCE_HVAC_CONTROL,      /**< Control loop state / bypass / lockout */
    ALARM_SOURCE_SYSTEM_INFRA,      /**< Ethernet, MicroSD, Flash, Modbus bus */
    ALARM_SOURCE_CUSTOM,            /**< Dynamically registered / application defined */
    ALARM_SOURCE_MAX                /**< Sentinel value */
} alarm_source_category_t;

/** Alias for alarm_source_category_t */
typedef alarm_source_category_t alarm_category_t;

/**
 * @brief State transition event types (ON_PRESS / ON_RELEASE semantics).
 */
typedef enum {
    ALARM_EVENT_NONE = 0,
    ALARM_EVENT_ACTIVATED,        /**< ON_PRESS: INACTIVE -> ACTIVE */
    ALARM_EVENT_CLEARED,          /**< ON_RELEASE: ACTIVE/ACK/LATCH -> INACTIVE */
    ALARM_EVENT_ACKNOWLEDGED,    /**< ACTIVE -> ACKNOWLEDGED */
    ALARM_EVENT_LATCHED,          /**< ACTIVE -> LATCHED */
    ALARM_EVENT_SOURCE_CHANGED,   /**< Contributing source added/removed in aggregate */
    ALARM_EVENT_MAX               /**< Sentinel value */
} alarm_transition_type_t;

/**
 * @brief Standardized well-known Alarm Identifiers.
 */
typedef enum {
    ALARM_ID_NONE = 0,

    /* Air Conditioner 1 Contacts (Unit 0) */
    ALARM_ID_AC1_HIGH_PRESSURE     = 0x0001,
    ALARM_ID_AC1_LOW_PRESSURE      = 0x0002,
    ALARM_ID_AC1_AIRFLOW_FAULT     = 0x0003,
    ALARM_ID_AC1_VOLTAGE_FAULT     = 0x0004,

    /* Air Conditioner 2 Contacts (Unit 1) */
    ALARM_ID_AC2_HIGH_PRESSURE     = 0x0005,
    ALARM_ID_AC2_LOW_PRESSURE      = 0x0006,
    ALARM_ID_AC2_AIRFLOW_FAULT     = 0x0007,
    ALARM_ID_AC2_VOLTAGE_FAULT     = 0x0008,

    /* Air Conditioner 3 Contacts (Unit 2) */
    ALARM_ID_AC3_HIGH_PRESSURE     = 0x0009,
    ALARM_ID_AC3_LOW_PRESSURE      = 0x000A,
    ALARM_ID_AC3_AIRFLOW_FAULT     = 0x000B,
    ALARM_ID_AC3_VOLTAGE_FAULT     = 0x000C,

    /* Air Conditioner 4 Contacts (Unit 3) */
    ALARM_ID_AC4_HIGH_PRESSURE     = 0x000D,
    ALARM_ID_AC4_LOW_PRESSURE      = 0x000E,
    ALARM_ID_AC4_AIRFLOW_FAULT     = 0x000F,
    ALARM_ID_AC4_VOLTAGE_FAULT     = 0x0010,

    /* Air Conditioner 5 Contacts (Unit 4) */
    ALARM_ID_AC5_HIGH_PRESSURE     = 0x0011,
    ALARM_ID_AC5_LOW_PRESSURE      = 0x0012,
    ALARM_ID_AC5_AIRFLOW_FAULT     = 0x0013,
    ALARM_ID_AC5_VOLTAGE_FAULT     = 0x0014,

    /* Air Conditioner 6 Contacts (Unit 5) */
    ALARM_ID_AC6_HIGH_PRESSURE     = 0x0015,
    ALARM_ID_AC6_LOW_PRESSURE      = 0x0016,
    ALARM_ID_AC6_AIRFLOW_FAULT     = 0x0017,
    ALARM_ID_AC6_VOLTAGE_FAULT     = 0x0018,

    /* Air Conditioner 7 Contacts (Unit 6) */
    ALARM_ID_AC7_HIGH_PRESSURE     = 0x0019,
    ALARM_ID_AC7_LOW_PRESSURE      = 0x001A,
    ALARM_ID_AC7_AIRFLOW_FAULT     = 0x001B,
    ALARM_ID_AC7_VOLTAGE_FAULT     = 0x001C,

    /* Air Conditioner 8 Contacts (Unit 7) */
    ALARM_ID_AC8_HIGH_PRESSURE     = 0x001D,
    ALARM_ID_AC8_LOW_PRESSURE      = 0x001E,
    ALARM_ID_AC8_AIRFLOW_FAULT     = 0x001F,
    ALARM_ID_AC8_VOLTAGE_FAULT     = 0x0020,

    /* Facility Safety Contacts */
    ALARM_ID_FACILITY_FIRE         = 0x0021,
    ALARM_ID_FACILITY_DOOR_OPEN    = 0x0022,
    ALARM_ID_FACILITY_WATER_LEAK   = 0x0023,
    ALARM_ID_FACILITY_SURGE_PROT   = 0x0024,
    ALARM_ID_FACILITY_PHASE_MON    = 0x0025,

    /* Consolidated Air Conditioner Summary Fault Alarms (AC1..AC8) */
    ALARM_ID_AC1_SUMMARY_FAULT     = 0x0026,
    ALARM_ID_AC2_SUMMARY_FAULT     = 0x0027,
    ALARM_ID_AC3_SUMMARY_FAULT     = 0x0028,
    ALARM_ID_AC4_SUMMARY_FAULT     = 0x0029,
    ALARM_ID_AC5_SUMMARY_FAULT     = 0x002A,
    ALARM_ID_AC6_SUMMARY_FAULT     = 0x002B,
    ALARM_ID_AC7_SUMMARY_FAULT     = 0x002C,
    ALARM_ID_AC8_SUMMARY_FAULT     = 0x002D,

    /* Environmental & Temperature Alarms (Groups 1..4 / Salas 1..4) */
    ALARM_ID_GROUP1_TEMP_LOW       = 0x0030,
    ALARM_ID_GROUP1_TEMP_HIGH      = 0x0031,
    ALARM_ID_GROUP1_TEMP_BOOST     = 0x0032,
    ALARM_ID_GROUP2_TEMP_LOW       = 0x0033,
    ALARM_ID_GROUP2_TEMP_HIGH      = 0x0034,
    ALARM_ID_GROUP2_TEMP_BOOST     = 0x0035,
    ALARM_ID_GROUP3_TEMP_LOW       = 0x0036,
    ALARM_ID_GROUP3_TEMP_HIGH      = 0x0037,
    ALARM_ID_GROUP3_TEMP_BOOST     = 0x0038,
    ALARM_ID_GROUP4_TEMP_LOW       = 0x0039,
    ALARM_ID_GROUP4_TEMP_HIGH      = 0x003A,
    ALARM_ID_GROUP4_TEMP_BOOST     = 0x003B,

    /* Electrical Power Quality Alarms */
    ALARM_ID_LINE_UNDER_VOLTAGE    = 0x0040,
    ALARM_ID_LINE_OVER_VOLTAGE     = 0x0041,
    ALARM_ID_ENERGY_METER_COMM_FAULT = 0x0042,

    /* HVAC Control & Equipment Alarms */
    ALARM_ID_HVAC_BYPASS_ACTIVE    = 0x0050,
    ALARM_ID_HVAC_G1_BYPASS_ACTIVE = 0x0050,
    ALARM_ID_HVAC_UNIT1_LOCKOUT    = 0x0051,
    ALARM_ID_HVAC_UNIT2_LOCKOUT    = 0x0052,
    ALARM_ID_HVAC_UNIT3_LOCKOUT    = 0x0053,
    ALARM_ID_HVAC_UNIT4_LOCKOUT    = 0x0054,
    ALARM_ID_HVAC_UNIT5_LOCKOUT    = 0x0055,
    ALARM_ID_HVAC_UNIT6_LOCKOUT    = 0x0056,
    ALARM_ID_HVAC_UNIT7_LOCKOUT    = 0x0057,
    ALARM_ID_HVAC_UNIT8_LOCKOUT    = 0x0058,
    ALARM_ID_HVAC_G2_BYPASS_ACTIVE = 0x0059,
    ALARM_ID_HVAC_G3_BYPASS_ACTIVE = 0x005A,
    ALARM_ID_HVAC_G4_BYPASS_ACTIVE = 0x005B,

    /* Sensor Hardware & Transducer Faults (Groups 1..4) */
    ALARM_ID_SENSOR_GROUP1_FAULT   = 0x0060,
    ALARM_ID_SENSOR_GROUP2_FAULT   = 0x0061,
    ALARM_ID_SENSOR_GROUP3_FAULT   = 0x0062,
    ALARM_ID_SENSOR_GROUP4_FAULT   = 0x0063,

    /* Infrastructure & System Faults */
    ALARM_ID_INFRA_ETHERNET_DOWN   = 0x0070,
    ALARM_ID_INFRA_SD_CARD_FAULT   = 0x0071,
    ALARM_ID_INFRA_MODBUS_FAULT    = 0x0072,

    /* Base for dynamic or expansion modules */
    ALARM_ID_CUSTOM_BASE           = 0x0100
} alarm_id_t;

/**
 * @brief Immutable transition event value object.
 *
 * Broadcasted over Zbus `chan_alarm_transition` whenever an alarm state changes.
 */
typedef struct {
    uint16_t                alarm_id;                  /**< Unique identifier of the alarm */
    alarm_transition_type_t event_type;                /**< Nature of transition (ACTIVATED, CLEARED, etc.) */
    alarm_state_t           previous_state;            /**< State prior to transition */
    alarm_state_t           new_state;                 /**< Resulting state */
    alarm_severity_t        severity;                  /**< Configured severity level */
    alarm_priority_t        priority;                  /**< Configured priority level */
    alarm_source_category_t source_category;           /**< Origin category */
    uint32_t                contributing_sources_mask; /**< Bitmask of sources (for aggregated alarms) */
    float                   trigger_value;             /**< Engineering value triggering event */
    uint64_t                timestamp_ms;              /**< Transition uptime timestamp */
} alarm_transition_event_t;

/**
 * @brief Immutable descriptor defining an alarm's metadata and behavior.
 */
typedef struct {
    uint16_t                alarm_id;            /**< Unique 16-bit alarm identifier */
    const char             *name;                /**< Short alphanumeric name (e.g., "AC1_HP") */
    const char             *description;         /**< Human-readable description */
    alarm_source_category_t source_category;     /**< Source category */
    alarm_severity_t        default_severity;    /**< Severity level */
    alarm_priority_t        default_priority;    /**< Priority level */
    bool                    is_latch_supported;  /**< If true, clears transition to LATCHED */
    bool                    is_ack_supported;    /**< If true, operator ack allowed */
    bool                    is_aggregated;       /**< If true, uses multi-source aggregation */
} alarm_descriptor_t;

/**
 * @brief Snapshot record of an alarm's current live state.
 */
typedef struct {
    uint16_t         alarm_id;                  /**< Unique alarm identifier */
    alarm_state_t    state;                     /**< Current state */
    alarm_severity_t severity;                  /**< Severity */
    uint32_t         contributing_sources_mask; /**< Active sources mask */
    float            last_trigger_value;        /**< Value at last trigger */
    uint64_t         triggered_timestamp_ms;    /**< Uptime when activated */
    uint64_t         cleared_timestamp_ms;      /**< Uptime when cleared */
    uint32_t         trigger_count;             /**< Cumulative activations */
    uint32_t         active_duration_ms;        /**< Cumulative or current active duration in ms */
} alarm_snapshot_entry_t;

/*============================================================================*
 * VALIDATION PREDICATES
 *============================================================================*/

static inline bool alarm_state_is_valid(alarm_state_t state)
{
    return ((unsigned int)state < (unsigned int)ALARM_STATE_MAX);
}

static inline bool alarm_severity_is_valid(alarm_severity_t severity)
{
    return ((unsigned int)severity < (unsigned int)ALARM_SEVERITY_MAX);
}

static inline bool alarm_priority_is_valid(alarm_priority_t priority)
{
    return ((unsigned int)priority < (unsigned int)ALARM_PRIORITY_MAX);
}

static inline bool alarm_source_category_is_valid(alarm_source_category_t cat)
{
    return ((unsigned int)cat < (unsigned int)ALARM_SOURCE_MAX);
}

static inline bool alarm_transition_type_is_valid(alarm_transition_type_t evt)
{
    return ((unsigned int)evt < (unsigned int)ALARM_EVENT_MAX);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_ALARM_TYPES_H_ */
