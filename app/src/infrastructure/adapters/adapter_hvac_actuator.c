/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_hvac_actuator.c
 * @brief Concrete adapter implementing i_hvac_unit_t via discrete relay ports.
 */

#include "infrastructure/adapters/adapter_hvac_actuator.h"
#include "common/default_config_profiles.h"

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
            endpoint->device_id != DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY) {
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

    /* P1-12: Unknown backend must return error, never return 0 silently */
    return -EINVAL;
}

static int adapter_hvac_actuator_set_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t tri)
{
    adapter_hvac_actuator_t *self = (adapter_hvac_actuator_t *)impl;

    if (self == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    if (!hvac_tri_state_is_valid((uint8_t)tri)) {
        return -EINVAL;
    }

    bool aa = false;
    bool sm = false;
    bool g = false;

    switch (tri) {
    case HVAC_TRI_STATE_OFF:
        aa = false;
        sm = true;
        g = false;
        break;

    case HVAC_TRI_STATE_ON:
        aa = true;
        sm = true;
        g = false;
        break;

    case HVAC_TRI_STATE_BYPASS:
        aa = false;
        sm = false;
        g = false;
        break;

    default:
        return -EINVAL;
    }

    const hvac_unit_hw_config_t *cfg = &self->hw_config[unit_id];
    int err;

    err = dispatch_endpoint_relay(self, &cfg->relay_aa, aa);
    if (err != 0) {
        return err;
    }

    err = dispatch_endpoint_relay(self, &cfg->relay_sm, sm);
    if (err != 0) {
        return err;
    }

    err = dispatch_endpoint_relay(self, &cfg->relay_g, g);
    if (err != 0) {
        return err;
    }

    self->cached_tri_state[unit_id] = tri;
    return 0;
}

static int adapter_hvac_actuator_get_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t *out_tri)
{
    adapter_hvac_actuator_t *self = (adapter_hvac_actuator_t *)impl;

    if (self == NULL || out_tri == NULL || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    *out_tri = self->cached_tri_state[unit_id];
    return 0;
}

static const i_hvac_unit_api_t s_actuator_api = {
    .set_tri_state = adapter_hvac_actuator_set_tri_state,
    .get_tri_state = adapter_hvac_actuator_get_tri_state,
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

    if (hw_config != NULL && count > 0U) {
        size_t n = (count < HVAC_MAX_UNITS_TOTAL) ? count : HVAC_MAX_UNITS_TOTAL;
        memcpy(self->hw_config, hw_config, n * sizeof(hvac_unit_hw_config_t));
    }

    if (remote_relay != NULL) {
        self->remote_relay_port = *remote_relay;
    }

    if (local_relay != NULL) {
        self->local_relay_port = *local_relay;
    }

    for (size_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        self->cached_tri_state[i] = HVAC_TRI_STATE_OFF;
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
