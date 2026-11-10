/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_unit_entity.h
 * @brief Pure domain model entity representing an individual AC unit.
 *
 * Encapsulates the unit-level state machine, anti-inrush startup delay,
 * pressure/electrical fault retry lockout, fan purge sub-FSM, and tri-state
 * output generation.
 *
 * Pure C99/C11: zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_DOMAIN_MODELS_HVAC_UNIT_ENTITY_H_
#define APP_DOMAIN_MODELS_HVAC_UNIT_ENTITY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fan control and post-purge sub-state machine states.
 *
 * Physical Relay G uses an NC contact in series with the thermostat G line:
 * - relay_g = 0 (LOW): Fan enabled / ON.
 * - relay_g = 1 (HIGH): Fan cut / OFF.
 */
typedef enum {
    HVAC_FAN_STATE_DISABLED = 0,       /**< Fan control disabled (relay_g = 0: fan always enabled) */
    HVAC_FAN_STATE_COMPRESSOR_RUNNING, /**< Compressor or forced fan active (relay_g = 0: fan ON) */
    HVAC_FAN_STATE_WAIT_DELAY,         /**< Compressor off, waiting purge timer (relay_g = 0: fan ON) */
    HVAC_FAN_STATE_SMART_IDLE_ON,      /**< Purge elapsed or idle with fan control ON (relay_g = 1: fan OFF) */
    HVAC_FAN_STATE_FIRE,               /**< Fire emergency shutdown (relay_g = 1: fan forced OFF) */
} hvac_fan_substate_t;

/**
 * @brief Individual AC unit entity state structure.
 */
typedef struct {
    /* Identification & Status */
    uint8_t             unit_id;       /**< Zero-indexed unit identifier */
    hvac_unit_state_t   state;         /**< Core unit operational state */
    hvac_fan_substate_t fan_state;     /**< Fan purge sub-state */

    /* Alarm Flags */
    bool high_pressure_alarm;          /**< High pressure switch trip */
    bool low_pressure_alarm;           /**< Low pressure switch trip */
    bool supply_fault_alarm;           /**< Airflow / water supply fault */
    bool voltage_fault_alarm;          /**< Phase / voltage monitor trip */

    /* Timers (seconds) */
    uint16_t startup_timer_s;          /**< Anti-inrush startup delay countdown */
    uint16_t fan_delay_timer_s;        /**< Fan purge delay countdown */
    uint16_t retry_timer_s;            /**< Alarm lockout retry countdown */

    /* Fault Counter & Limits */
    uint8_t fault_counter;             /**< Accumulated consecutive faults */
    uint8_t max_allowed_faults;        /**< Fault ceiling before permanent eviction */

    /* Static Configuration */
    uint16_t startup_delay_s;          /**< Configured anti-inrush startup delay */
    uint16_t fan_delay_s;              /**< Configured fan post-cooling purge delay */
    uint16_t pressure_retry_s;         /**< Lockout retry delay (default 180s) */
    bool     enable_fan_delay;         /**< Fan purge feature flag */
} hvac_unit_entity_t;

/**
 * @brief Initialize an AC unit entity.
 *
 * @param[out] self              Unit entity instance.
 * @param[in]  unit_id           Zero-indexed unit identifier.
 * @param[in]  max_faults        Maximum faults before permanent eviction.
 * @param[in]  startup_delay_s   Default startup delay in seconds.
 * @param[in]  fan_delay_s       Fan purge delay in seconds.
 * @param[in]  pressure_retry_s  Alarm lockout duration in seconds.
 * @param[in]  enable_fan_delay  Enable fan purge cycle.
 */
void hvac_unit_entity_init(hvac_unit_entity_t *self,
                           uint8_t unit_id,
                           uint8_t max_faults,
                           uint16_t startup_delay_s,
                           uint16_t fan_delay_s,
                           uint16_t pressure_retry_s,
                           bool enable_fan_delay);

/**
 * @brief Update physical safety alarms for the unit.
 *
 * @param[in,out] self          Unit entity instance.
 * @param[in]     high_press    High pressure alarm active.
 * @param[in]     low_press     Low pressure alarm active.
 * @param[in]     supply_fault  Airflow / water supply fault active.
 * @param[in]     volt_fault    Voltage / phase monitor fault active.
 */
void hvac_unit_entity_update_alarms(hvac_unit_entity_t *self,
                                    bool high_press,
                                    bool low_press,
                                    bool supply_fault,
                                    bool volt_fault);

/**
 * @brief Record a fault event, incrementing the fault counter and enforcing lockout.
 *
 * @param[in,out] self Unit entity instance.
 */
void hvac_unit_entity_increment_fault(hvac_unit_entity_t *self);

/**
 * @brief 1-second periodic tick handler for timers and transitions.
 *
 * @param[in,out] self Unit entity instance.
 */
void hvac_unit_entity_tick_1s(hvac_unit_entity_t *self);

/**
 * @brief Request the unit to start using its default configured startup delay.
 *
 * @param[in,out] self Unit entity instance.
 * @return true if start accepted or running, false if blocked by fault/alarm.
 */
bool hvac_unit_entity_request_start(hvac_unit_entity_t *self);

/**
 * @brief Request the unit to start with an explicit staggered anti-inrush delay.
 *
 * @param[in,out] self     Unit entity instance.
 * @param[in]     delay_s  Staggered delay in seconds (0 = immediate start).
 * @return true if start accepted or running, false if blocked by fault/alarm.
 */
bool hvac_unit_entity_request_start_staggered(hvac_unit_entity_t *self, uint16_t delay_s);

/**
 * @brief Request the unit to stop cooling.
 *
 * @param[in,out] self              Unit entity instance.
 * @param[in]     enable_fan_delay  Whether to initiate fan purge sub-FSM.
 */
void hvac_unit_entity_request_stop(hvac_unit_entity_t *self, bool enable_fan_delay);

/**
 * @brief Force the unit into mechanical bypass mode.
 *
 * @param[in,out] self Unit entity instance.
 */
void hvac_unit_entity_set_bypass(hvac_unit_entity_t *self);

/**
 * @brief Apply manual override to the unit.
 *
 * @param[in,out] self     Unit entity instance.
 * @param[in]     turn_on  true to force compressor ON, false to turn OFF.
 */
void hvac_unit_entity_set_manual(hvac_unit_entity_t *self, bool turn_on);

/**
 * @brief Query the synthesized tri-state command level of the unit.
 *
 * @param[in] self Unit entity instance.
 * @return Tri-state command level (OFF, ON, BYPASS).
 */
hvac_tri_state_t hvac_unit_entity_get_tri_state(const hvac_unit_entity_t *self);

/**
 * @brief Obtain discrete physical relay command states (AA, SM, G).
 *
 * @param[in]  self      Unit entity instance.
 * @param[out] relay_aa  Compressor contactor state.
 * @param[out] relay_sm  Smart/Bypass selector relay state (1 = Smart, 0 = Bypass).
 * @param[out] relay_g   Fan purge relay state.
 */
void hvac_unit_entity_get_relays(const hvac_unit_entity_t *self,
                                 bool *relay_aa,
                                 bool *relay_sm,
                                 bool *relay_g);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_MODELS_HVAC_UNIT_ENTITY_H_ */
