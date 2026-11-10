/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_hvac_actuator.h
 * @brief Concrete adapter implementing i_hvac_unit_t via discrete relay ports.
 *
 * Translates high-level tri-state commands (OFF, ON, BYPASS) to individual
 * relay contacts (relay_aa, relay_sm, relay_g) mapped across remote Modbus
 * RTU relay modules (e.g. Waveshare 32CH) and optional local GPIO relays.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_HVAC_ACTUATOR_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_HVAC_ACTUATOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"
#include "interfaces/i_hvac_unit.h"
#include "interfaces/i_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HVAC actuator adapter context.
 */
typedef struct adapter_hvac_actuator_s adapter_hvac_actuator_t;

/**
 * @brief HVAC actuator adapter context.
 */
struct adapter_hvac_actuator_s {
    hvac_unit_hw_config_t     hw_config[HVAC_MAX_UNITS_TOTAL];
    i_relay_t                 remote_relay_port;
    i_relay_t                 local_relay_port;
    hvac_tri_state_t          cached_tri_state[HVAC_MAX_UNITS_TOTAL];
    bool                      cache_valid[HVAC_MAX_UNITS_TOTAL];
    hvac_alarm_contact_type_t bypass_alarm_contact_type;
};

/**
 * @brief Initialize the HVAC actuator adapter.
 *
 * @param[out] self         Adapter instance to initialize.
 * @param[in]  hw_config    Array of hardware routing descriptors (may be NULL).
 * @param[in]  count        Number of descriptors in @p hw_config (up to HVAC_MAX_UNITS_TOTAL).
 * @param[in]  remote_relay Bound remote relay port handle (may be NULL if unassigned).
 * @param[in]  local_relay  Bound local relay port handle (may be NULL if unassigned).
 * @return 0 on success, -EINVAL on NULL @p self.
 */
int adapter_hvac_actuator_init(adapter_hvac_actuator_t *self,
                               const hvac_unit_hw_config_t *hw_config,
                               size_t count,
                               const i_relay_t *remote_relay,
                               const i_relay_t *local_relay);

/**
 * @brief Update the hardware routing configuration for a specific AC unit.
 *
 * @param[in,out] self    Adapter instance.
 * @param[in]     unit_id Zero-indexed unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[in]     cfg     Pointer to unit hardware routing configuration.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int adapter_hvac_actuator_set_hw_config(adapter_hvac_actuator_t *self,
                                        uint8_t unit_id,
                                        const hvac_unit_hw_config_t *cfg);

/**
 * @brief Obtain the polymorphic i_hvac_unit_t port handle.
 *
 * @param[in] self Adapter instance.
 * @return Bound i_hvac_unit_t handle, or unbound handle if @p self is NULL.
 */
i_hvac_unit_t adapter_hvac_actuator_port(adapter_hvac_actuator_t *self);

/**
 * @brief Drive the bypass alarm contact (Alarm Relay 1) via local relay port.
 *
 * @param[in] impl    Adapter instance context.
 * @param[in] active  true when system is in Bypass, false when in Smart mode.
 * @return 0 on success, negative errno on failure.
 */
int adapter_hvac_actuator_set_bypass_alarm(void *impl, bool active);

/**
 * @brief Configure the contact polarity for the bypass alarm relay (Alarm Out 1).
 *
 * @param[in,out] self          Adapter instance.
 * @param[in]     contact_type  HVAC_ALARM_CONTACT_NC (0=Bypass, 1=Smart) or HVAC_ALARM_CONTACT_NO (1=Bypass, 0=Smart).
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int adapter_hvac_actuator_set_bypass_alarm_contact_type(adapter_hvac_actuator_t *self,
                                                        hvac_alarm_contact_type_t contact_type);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_HVAC_ACTUATOR_H_ */
