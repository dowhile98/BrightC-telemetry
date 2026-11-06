/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file digital_input_types.h
 * @brief Domain types and Software Integrator Debounce Filter for Digital Inputs.
 *
 * Adheres to Clean Architecture (§4.1.D): pure ISO C99/C11 domain types with
 * zero Zephyr OS headers and zero dynamic memory allocation.
 */

#ifndef APP_COMMON_DIGITAL_INPUT_TYPES_H_
#define APP_COMMON_DIGITAL_INPUT_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum supported logical digital input channels in the system. */
#define DI_CHANNEL_MAX 64U

/** Default contactor debounce time in milliseconds (Revision F §2.3). */
#define DI_DEBOUNCE_CONTACTOR_MS 500U

/** Default alarm contact debounce time in milliseconds. */
#define DI_DEBOUNCE_ALARM_MS 60U

/**
 * @brief Semantic roles for digital inputs in BrightC V3.
 */
typedef enum {
    /* HVAC Fan & Compressor Feedback (Revision F Model) */
    DI_ROLE_FAN_1_AUX = 0,           /**< Evaporator Fan 1 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_2_AUX,               /**< Evaporator Fan 2 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_3_AUX,               /**< Evaporator Fan 3 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_4_AUX,               /**< Evaporator Fan 4 Run Feedback (U_vent_ON) */
    DI_ROLE_COMPRESSOR_1_AUX,        /**< Compressor 1 Run Auxiliary Contact (c1) */
    DI_ROLE_COMPRESSOR_2_AUX,        /**< Compressor 2 Run Auxiliary Contact (c2) */
    DI_ROLE_COMPRESSOR_3_AUX,        /**< Compressor 3 Run Auxiliary Contact (c3) */
    DI_ROLE_COMPRESSOR_4_AUX,        /**< Compressor 4 Run Auxiliary Contact (c4) */

    /* HVAC Safety Interlocks */
    DI_ROLE_HIGH_PRESSURE_1,         /**< High Pressure Cutout Switch Circuit 1 */
    DI_ROLE_HIGH_PRESSURE_2,         /**< High Pressure Cutout Switch Circuit 2 */
    DI_ROLE_LOW_PRESSURE_1,          /**< Low Pressure Cutout Switch Circuit 1 */
    DI_ROLE_LOW_PRESSURE_2,          /**< Low Pressure Cutout Switch Circuit 2 */
    DI_ROLE_THERMAL_OVERLOAD_FAN,    /**< Evaporator Fan Motor Overload Trip */
    DI_ROLE_THERMAL_OVERLOAD_COMP,   /**< Compressor Motor Overload Trip */

    /* Facility & Environmental Alarms */
    DI_ROLE_WATER_LEAK,              /**< Raised Floor Water Inundation Sensor */
    DI_ROLE_SMOKE_FIRE,              /**< Facility Smoke / Fire Alarm Interlock */
    DI_ROLE_PHASE_MONITOR,           /**< 3-Phase Power Sequence / Loss Relay */
    DI_ROLE_SURGE_PROTECTOR,         /**< Lightning / Surge Protective Device (DPS) */
    DI_ROLE_DOOR_OPEN,               /**< Containment Aisle Door Contact */

    /* Generic User & Expansion Inputs */
    DI_ROLE_USER_1,
    DI_ROLE_USER_2,
    DI_ROLE_USER_3,
    DI_ROLE_USER_4,
    DI_ROLE_USER_BUTTON,
    DI_ROLE_BYPASS_SWITCH,

    /* HVAC Unit Alarm Inputs (Unit 0..3 for 4-unit hybrid, Unit 4..7 for 8-unit control-only) */
    DI_ROLE_UNIT_0_ALARM_HP,
    DI_ROLE_UNIT_0_ALARM_LP,
    DI_ROLE_UNIT_0_ALARM_FLOW,
    DI_ROLE_UNIT_0_ALARM_VOLT,
    DI_ROLE_UNIT_1_ALARM_HP,
    DI_ROLE_UNIT_1_ALARM_LP,
    DI_ROLE_UNIT_1_ALARM_FLOW,
    DI_ROLE_UNIT_1_ALARM_VOLT,
    DI_ROLE_UNIT_2_ALARM_HP,
    DI_ROLE_UNIT_2_ALARM_LP,
    DI_ROLE_UNIT_2_ALARM_FLOW,
    DI_ROLE_UNIT_2_ALARM_VOLT,
    DI_ROLE_UNIT_3_ALARM_HP,
    DI_ROLE_UNIT_3_ALARM_LP,
    DI_ROLE_UNIT_3_ALARM_FLOW,
    DI_ROLE_UNIT_3_ALARM_VOLT,
    DI_ROLE_UNIT_4_ALARM_HP,
    DI_ROLE_UNIT_4_ALARM_LP,
    DI_ROLE_UNIT_4_ALARM_FLOW,
    DI_ROLE_UNIT_4_ALARM_VOLT,
    DI_ROLE_UNIT_5_ALARM_HP,
    DI_ROLE_UNIT_5_ALARM_LP,
    DI_ROLE_UNIT_5_ALARM_FLOW,
    DI_ROLE_UNIT_5_ALARM_VOLT,
    DI_ROLE_UNIT_6_ALARM_HP,
    DI_ROLE_UNIT_6_ALARM_LP,
    DI_ROLE_UNIT_6_ALARM_FLOW,
    DI_ROLE_UNIT_6_ALARM_VOLT,
    DI_ROLE_UNIT_7_ALARM_HP,
    DI_ROLE_UNIT_7_ALARM_LP,
    DI_ROLE_UNIT_7_ALARM_FLOW,
    DI_ROLE_UNIT_7_ALARM_VOLT,

    DI_ROLE_MAX
} di_role_t;

/* Convenience aliases for unit alarms */
#define DI_ROLE_UNIT_0_HP   DI_ROLE_UNIT_0_ALARM_HP
#define DI_ROLE_UNIT_0_LP   DI_ROLE_UNIT_0_ALARM_LP
#define DI_ROLE_UNIT_0_FLOW DI_ROLE_UNIT_0_ALARM_FLOW
#define DI_ROLE_UNIT_0_VOLT DI_ROLE_UNIT_0_ALARM_VOLT

#define DI_ROLE_UNIT_1_HP   DI_ROLE_UNIT_1_ALARM_HP
#define DI_ROLE_UNIT_1_LP   DI_ROLE_UNIT_1_ALARM_LP
#define DI_ROLE_UNIT_1_FLOW DI_ROLE_UNIT_1_ALARM_FLOW
#define DI_ROLE_UNIT_1_VOLT DI_ROLE_UNIT_1_ALARM_VOLT

#define DI_ROLE_UNIT_2_HP   DI_ROLE_UNIT_2_ALARM_HP
#define DI_ROLE_UNIT_2_LP   DI_ROLE_UNIT_2_ALARM_LP
#define DI_ROLE_UNIT_2_FLOW DI_ROLE_UNIT_2_ALARM_FLOW
#define DI_ROLE_UNIT_2_VOLT DI_ROLE_UNIT_2_ALARM_VOLT

#define DI_ROLE_UNIT_3_HP   DI_ROLE_UNIT_3_ALARM_HP
#define DI_ROLE_UNIT_3_LP   DI_ROLE_UNIT_3_ALARM_LP
#define DI_ROLE_UNIT_3_FLOW DI_ROLE_UNIT_3_ALARM_FLOW
#define DI_ROLE_UNIT_3_VOLT DI_ROLE_UNIT_3_ALARM_VOLT

/**
 * @brief Physical hardware source for a digital input channel.
 */
typedef enum {
    DI_SOURCE_INTERNAL_SPI = 0,      /**< On-board MCP23S17 SPI expander pin */
    DI_SOURCE_EXTERNAL_MODBUS,       /**< Remote Modbus RTU I/O module register bit */
    DI_SOURCE_ONCHIP_GPIO,           /**< Microcontroller on-chip GPIO pin */
    DI_SOURCE_MOCK                   /**< Software simulated input for unit testing */
} di_source_type_t;

/**
 * @brief Electrical signal assertion level.
 */
typedef enum {
    DI_ACTIVE_HIGH = 0,              /**< Voltage present (High logic) asserts input */
    DI_ACTIVE_LOW                    /**< Voltage absent / GND (Low logic) asserts input */
} di_active_level_t;

/**
 * @brief Physical mechanical contact configuration.
 */
typedef enum {
    DI_CONTACT_NORMALLY_OPEN = 0,    /**< NO: Contact closes when event occurs */
    DI_CONTACT_NORMALLY_CLOSED       /**< NC: Contact opens when event occurs (fail-safe) */
} di_contact_type_t;

/**
 * @brief Normalized logical digital input state.
 */
typedef enum {
    DI_STATE_INACTIVE = 0,           /**< Normal operating condition / Contact de-asserted */
    DI_STATE_ACTIVE = 1              /**< Active condition / Alarm asserted / Contactor closed */
} di_state_t;

/*============================================================================*
 * SOFTWARE INTEGRATOR DEBOUNCE FILTER
 *============================================================================*/

/**
 * @brief Pure C99 Software Integrator Debounce Filter State.
 */
typedef struct {
    bool     stable_state;           /**< Confirmed debounced output state */
    bool     raw_last_state;         /**< Preceding raw sample level */
    uint32_t stable_counter_ms;      /**< Accumulated continuous stability in ms */
    uint16_t debounce_ms;            /**< Required stable duration (0 = immediate bypass) */
} debounce_filter_t;

/**
 * @brief Initialize a debounce filter instance.
 *
 * @param[out] filter         Pointer to filter state structure.
 * @param[in]  debounce_ms    Required stability duration in ms (0 = immediate bypass).
 * @param[in]  initial_state  Initial stable and raw state.
 */
static inline void debounce_filter_init(debounce_filter_t *filter, uint16_t debounce_ms, bool initial_state)
{
    if (filter == NULL) {
        return;
    }
    filter->debounce_ms = debounce_ms;
    filter->stable_state = initial_state;
    filter->raw_last_state = initial_state;
    filter->stable_counter_ms = 0U;
}

/**
 * @brief Update the debounce filter with a newly acquired raw sample.
 *
 * @param[in,out] filter          Pointer to filter state structure.
 * @param[in]     raw_state       Latest raw digital sample.
 * @param[in]     scan_period_ms  Time elapsed since previous sample (e.g. 20 ms).
 * @param[out]    out_stable      Receives current confirmed stable state (may be NULL).
 * @return true if an output state transition was confirmed on this cycle; false otherwise.
 */
static inline bool debounce_filter_update(debounce_filter_t *filter,
                                          bool raw_state,
                                          uint32_t scan_period_ms,
                                          bool *out_stable)
{
    if (filter == NULL) {
        if (out_stable != NULL) {
            *out_stable = false;
        }
        return false;
    }

    /* Zero-Debounce Bypass (e.g. pre-filtered Modbus inputs) */
    if (filter->debounce_ms == 0U) {
        if (filter->stable_state != raw_state) {
            filter->stable_state = raw_state;
            filter->raw_last_state = raw_state;
            filter->stable_counter_ms = 0U;
            if (out_stable != NULL) {
                *out_stable = filter->stable_state;
            }
            return true;
        }
        if (out_stable != NULL) {
            *out_stable = filter->stable_state;
        }
        return false;
    }

    /* Integrator Accumulation */
    if (raw_state == filter->raw_last_state) {
        filter->stable_counter_ms += scan_period_ms;
    } else {
        filter->raw_last_state = raw_state;
        filter->stable_counter_ms = 0U; /* Glitch detected: reset accumulator */
    }

    /* Threshold Evaluation */
    if (filter->stable_counter_ms >= (uint32_t)filter->debounce_ms) {
        if (filter->stable_state != raw_state) {
            filter->stable_state = raw_state;
            if (out_stable != NULL) {
                *out_stable = filter->stable_state;
            }
            return true;
        }
    }

    if (out_stable != NULL) {
        *out_stable = filter->stable_state;
    }
    return false;
}

/*============================================================================*
 * LOGICAL CHANNEL CONFIGURATION & STATUS
 *============================================================================*/

/**
 * @brief Static configuration of one logical digital input channel.
 */
typedef struct {
    di_role_t         role;          /**< Semantic role */
    di_source_type_t  source_type;   /**< INTERNAL_SPI, EXTERNAL_MODBUS, MOCK */
    uint8_t           source_dev_id; /**< Chip index (0..3 for MCP23S17) or Modbus slave ID */
    uint8_t           pin_index;     /**< Expander pin (0..15) or Modbus bit offset */
    di_active_level_t active_level;  /**< ACTIVE_HIGH or ACTIVE_LOW */
    di_contact_type_t contact_type;  /**< NORMALLY_OPEN or NORMALLY_CLOSED */
    uint16_t          debounce_ms;   /**< Stability duration in ms (0 = bypass) */
    bool              enabled;       /**< Channel active flag */
} di_channel_config_t;

/**
 * @brief Runtime status and diagnostic metrics for one channel.
 */
typedef struct {
    di_role_t role;                  /**< Semantic role */
    bool      raw_level;             /**< Most recent un-debounced pin level */
    bool      is_active;             /**< Confirmed normalized active state */
    uint32_t  active_duration_s;     /**< Seconds spent continuously in active state */
    uint32_t  transition_count;      /**< Lifetime state change counter */
    uint64_t  last_transition_ms;    /**< Uptime timestamp of last state change */
} di_channel_status_t;

/**
 * @brief Zbus Event payload emitted on state transition.
 */
typedef struct {
    di_role_t  role;                 /**< Channel that transitioned */
    di_state_t new_state;            /**< New confirmed state (ACTIVE / INACTIVE) */
    di_state_t prev_state;           /**< Previous confirmed state */
    uint64_t   timestamp_ms;         /**< Kernel uptime when confirmed */
} di_transition_event_t;

/*============================================================================*
 * POLARITY NORMALIZATION & VALIDATION HELPERS
 *============================================================================*/

/**
 * @brief Normalize a raw physical pin level into an asserted logical state.
 *
 * @param[in] raw_pin_level Physical logic level of the input pin (0 or 1).
 * @param[in] active_level  Electrical assertion configuration (ACTIVE_HIGH or ACTIVE_LOW).
 * @param[in] contact_type  Contact configuration (NORMALLY_OPEN or NORMALLY_CLOSED).
 * @return true if the contact/alarm is asserted; false if normal/quiescent.
 */
static inline bool di_normalize_raw_level(bool raw_pin_level,
                                          di_active_level_t active_level,
                                          di_contact_type_t contact_type)
{
    bool is_asserted = (active_level == DI_ACTIVE_HIGH) ? raw_pin_level : !raw_pin_level;
    if (contact_type == DI_CONTACT_NORMALLY_CLOSED) {
        is_asserted = !is_asserted;
    }
    return is_asserted;
}

/**
 * @brief Validate semantic role.
 */
static inline bool di_role_is_valid(di_role_t role)
{
    return ((unsigned int)role < (unsigned int)DI_ROLE_MAX);
}

/**
 * @brief Validate physical source type.
 */
static inline bool di_source_type_is_valid(di_source_type_t source)
{
    return (source == DI_SOURCE_INTERNAL_SPI ||
            source == DI_SOURCE_EXTERNAL_MODBUS ||
            source == DI_SOURCE_ONCHIP_GPIO ||
            source == DI_SOURCE_MOCK);
}

/**
 * @brief Validate active level setting.
 */
static inline bool di_active_level_is_valid(di_active_level_t level)
{
    return (level == DI_ACTIVE_HIGH || level == DI_ACTIVE_LOW);
}

/**
 * @brief Validate contact type setting.
 */
static inline bool di_contact_type_is_valid(di_contact_type_t contact)
{
    return (contact == DI_CONTACT_NORMALLY_OPEN || contact == DI_CONTACT_NORMALLY_CLOSED);
}

/**
 * @brief Validate digital input state.
 */
static inline bool di_state_is_valid(di_state_t state)
{
    return (state == DI_STATE_INACTIVE || state == DI_STATE_ACTIVE);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_DIGITAL_INPUT_TYPES_H_ */
