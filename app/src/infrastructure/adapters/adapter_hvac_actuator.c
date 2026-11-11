/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_hvac_actuator.c
 * @brief Concrete adapter implementing i_hvac_unit_t via discrete relay ports.
 */

#include "infrastructure/adapters/adapter_hvac_actuator.h"
#include "infrastructure/adapters/adapter_relay_gpio.h"
#include "common/default_config_profiles.h"
#include "common/hardware_pin_map.h"

#include <errno.h>
#include <string.h>

static int dispatch_endpoint_relay(adapter_hvac_actuator_t *self,
                                   const hvac_io_endpoint_t *endpoint,
                                   bool state)
{
    if (endpoint == NULL || endpoint->backend == (uint8_t)HVAC_IO_BACKEND_NONE) {
        return 0;
    }

    relay_state_t r_state = state ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;

    if (endpoint->backend == (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS) {
        /* Route to remote Modbus expansion relay (e.g. Waveshare 32CH) */
        if (!i_relay_is_bound(&self->remote_relay_port)) {
            return -ENODEV;
        }
        /* Validate device_id routing: accept default (0) or Waveshare relay slave ID */
        if (endpoint->device_id != 0U &&
            endpoint->device_id != MODBUS_ADDR_WAVESHARE_RELAY) {
            return -ENODEV;
        }
        return i_relay_set_state(&self->remote_relay_port,
                                 (relay_channel_t)endpoint->channel,
                                 r_state);
    }

    if (endpoint->backend == (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO ||
        endpoint->backend == (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17) {
        /* Route to local on-board relay port (direct GPIO or SPI expander mcp23s17_2) */
        if (!i_relay_is_bound(&self->local_relay_port)) {
            return -ENODEV;
        }
        /* Validate device_id routing: accept default (0) or on-board expander index (<= 2) */
        if (endpoint->device_id > 2U) {
            return -ENODEV;
        }
        return i_relay_set_state(&self->local_relay_port,
                                 (relay_channel_t)endpoint->channel,
                                 r_state);
    }

    /* Unknown backend must return error, never return 0 silently */
    return -EINVAL;
}

static int adapter_hvac_actuator_set_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t tri)
{
    adapter_hvac_actuator_t *self = (adapter_hvac_actuator_t *)impl;

    if (self == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    if (!hvac_tri_state_is_valid(tri)) {
        return -EINVAL;
    }

    if (self->cache_valid[unit_id] && hvac_tri_state_is_equal(self->cached_tri_state[unit_id], tri)) {
        return 0;
    }

    const hvac_unit_hw_config_t *cfg = &self->hw_config[unit_id];
    int err;

    err = dispatch_endpoint_relay(self, &cfg->relay_aa, tri.relay_aa);
    if (err != 0) {
        return err;
    }

    err = dispatch_endpoint_relay(self, &cfg->relay_sm, tri.relay_sm);
    if (err != 0) {
        return err;
    }

    err = dispatch_endpoint_relay(self, &cfg->relay_g, tri.relay_g);
    if (err != 0) {
        return err;
    }

    self->cached_tri_state[unit_id] = tri;
    self->cache_valid[unit_id] = true;
    return 0;
}

static int adapter_hvac_actuator_get_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t *out_tri)
{
    adapter_hvac_actuator_t *self = (adapter_hvac_actuator_t *)impl;

    if (self == NULL || out_tri == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    if (!self->cache_valid[unit_id] || !hvac_tri_state_is_valid(self->cached_tri_state[unit_id])) {
        *out_tri = HVAC_TRI_STATE_OFF;
    } else {
        *out_tri = self->cached_tri_state[unit_id];
    }
    return 0;
}

int adapter_hvac_actuator_set_bypass_alarm(void *impl, bool active)
{
    adapter_hvac_actuator_t *self = (adapter_hvac_actuator_t *)impl;

    if (self == NULL) {
        return -EINVAL;
    }

    if (!i_relay_is_bound(&self->local_relay_port)) {
        return 0;
    }

    relay_state_t r_state;
    if (self->bypass_alarm_contact_type == HVAC_ALARM_CONTACT_NO) {
        r_state = active ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    } else {
        /* Default NC (Ref 1 & Ref 2 hardware polarity):
         * Smart mode (!active) -> RELAY_STATE_CLOSED (1), Bypass (active) -> RELAY_STATE_OPEN (0)
         */
        r_state = active ? RELAY_STATE_OPEN : RELAY_STATE_CLOSED;
    }

    return i_relay_set_state(&self->local_relay_port,
                             (relay_channel_t)RELAY_CH_ALARM_OUT_1,
                             r_state);
}

int adapter_hvac_actuator_set_bypass_alarm_contact_type(adapter_hvac_actuator_t *self,
                                                        hvac_alarm_contact_type_t contact_type)
{
    if (self == NULL || !hvac_alarm_contact_type_is_valid((uint8_t)contact_type)) {
        return -EINVAL;
    }

    self->bypass_alarm_contact_type = contact_type;
    return 0;
}

static const i_hvac_unit_api_t s_actuator_api = {
    .set_tri_state = adapter_hvac_actuator_set_tri_state,
    .get_tri_state = adapter_hvac_actuator_get_tri_state,
    .set_bypass_alarm = adapter_hvac_actuator_set_bypass_alarm,
};

int adapter_hvac_actuator_init(adapter_hvac_actuator_t *self,
                               const hvac_unit_hw_config_t *hw_config,
                               size_t count,
                               const i_relay_t *remote_relay,
                               const i_relay_t *local_relay)
{
    if (self == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->bypass_alarm_contact_type = HVAC_ALARM_CONTACT_NC;

    if (hw_config != NULL && count > 0U) {
        size_t n = (count < HVAC_MAX_UNITS_TOTAL) ? count : HVAC_MAX_UNITS_TOTAL;
        memcpy(self->hw_config, hw_config, n * sizeof(hvac_unit_hw_config_t));
    } else {
        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            const hvac_unit_hw_config_t *u_cfg = hardware_pin_map_get_unit_for_mode(HVAC_MODE_HYBRID_FULL, u);
            if (u_cfg != NULL) {
                self->hw_config[u] = *u_cfg;
            }
        }
    }

    if (remote_relay != NULL) {
        self->remote_relay_port = *remote_relay;
    }

    if (local_relay != NULL) {
        self->local_relay_port = *local_relay;
    }

    for (size_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        self->cached_tri_state[i] = HVAC_TRI_STATE_OFF;
        self->cache_valid[i] = false;
    }

    return 0;
}

int adapter_hvac_actuator_init_for_mode(adapter_hvac_actuator_t *self,
                                        hvac_operation_mode_t mode,
                                        const i_relay_t *remote_relay,
                                        const i_relay_t *local_relay)
{
    if (self == NULL || !hvac_operation_mode_is_valid((uint8_t)mode)) {
        return -EINVAL;
    }

    int err = adapter_hvac_actuator_init(self, NULL, 0U, remote_relay, local_relay);
    if (err != 0) {
        return err;
    }

    return adapter_hvac_actuator_apply_mode(self, mode);
}

int adapter_hvac_actuator_apply_mode(adapter_hvac_actuator_t *self, hvac_operation_mode_t mode)
{
    if (self == NULL || !hvac_operation_mode_is_valid((uint8_t)mode)) {
        return -EINVAL;
    }

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        const hvac_unit_hw_config_t *u_cfg = hardware_pin_map_get_unit_for_mode(mode, u);
        if (u_cfg != NULL) {
            self->hw_config[u] = *u_cfg;
        }
        self->cached_tri_state[u] = HVAC_TRI_STATE_OFF;
        self->cache_valid[u] = false;
    }

    return 0;
}

int adapter_hvac_actuator_set_hw_config(adapter_hvac_actuator_t *self,
                                        uint8_t unit_id,
                                        const hvac_unit_hw_config_t *cfg)
{
    if (self == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL || cfg == NULL) {
        return -EINVAL;
    }

    self->hw_config[unit_id] = *cfg;
    self->cached_tri_state[unit_id] = HVAC_TRI_STATE_OFF;
    self->cache_valid[unit_id] = false;
    return 0;
}

i_hvac_unit_t adapter_hvac_actuator_port(adapter_hvac_actuator_t *self)
{
    i_hvac_unit_t port = {
        .api = (self != NULL) ? &s_actuator_api : NULL,
        .impl = self,
    };
    return port;
}
