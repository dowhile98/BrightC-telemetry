/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adapter_relay_modbus.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_relay_modbus, LOG_LEVEL_INF);

/**
 * @brief Translate a relay_state_t into an FC05 coil value.
 */
static uint16_t state_to_coil_value(relay_state_t state)
{
    switch (state) {
    case RELAY_STATE_CLOSED:
        return WAVESHARE_RELAY_COIL_ON;
    case RELAY_STATE_OPEN:
    default:
        return WAVESHARE_RELAY_COIL_OFF;
    }
}

static int relay_modbus_set_state(void *impl, relay_channel_t channel,
                                  relay_state_t state)
{
    adapter_relay_modbus_t *self = (adapter_relay_modbus_t *) impl;

    if (self == NULL || self->ao == NULL || self->ao->queue == NULL ||
        channel >= WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    uint16_t coil_value = state_to_coil_value(state);

    modbus_event_t event = {
        .type = AO_MODBUS_EVENT_WRITE_COIL,
        .device_id = 0U,
        .slave_addr = self->slave_addr,
        .coil_addr = (uint16_t) channel,
        .coil_value = coil_value,
        .timeout_ms = self->timeout_ms,
    };

    /* Attempt to post with up to 3 retries if the queue is full */
    int post_err = -ENOMSG;
    for (int retry = 0; retry < 3; retry++) {
        post_err = ao_modbus_post(self->ao, &event);
        if (post_err == 0) {
            break;
        }
        if (!k_is_in_isr()) {
            k_msleep(10);
        }
    }

    if (post_err != 0) {
        LOG_ERR("relay: command queue full after retries, write failed for channel %u",
                (unsigned int)channel);
        return -ENOMSG;
    }

    /* Update the local cache non-optimistically only upon successful enqueue;
     * periodic FC01 poll will reconcile with the real physical device state. */
    self->cache[channel] = state;
    self->cache_valid = true;
    self->cycle_count[channel]++;
    self->last_toggle_ms[channel] = (uint32_t)k_uptime_get();

    return 0;
}

static int relay_modbus_get_state(void *impl, relay_channel_t channel,
                                  relay_state_t *out_state)
{
    adapter_relay_modbus_t *self = (adapter_relay_modbus_t *) impl;

    if (self == NULL || out_state == NULL ||
        channel >= WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    *out_state = self->cache[channel];
    return 0;
}

static int relay_api_get_status(void *impl, relay_channel_t channel,
                                relay_channel_status_t *out_status)
{
    adapter_relay_modbus_t *self = (adapter_relay_modbus_t *) impl;

    if (self == NULL || out_status == NULL ||
        channel >= WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    out_status->state = self->cache[channel];
    out_status->cycle_count = self->cycle_count[channel];
    out_status->last_toggle_ms = self->last_toggle_ms[channel];
    out_status->is_fault = self->is_fault[channel];

    return 0;
}

static const i_relay_api_t k_adapter_api = {
    .set_state = relay_modbus_set_state,
    .get_state = relay_modbus_get_state,
    .get_status = relay_api_get_status,
};

int adapter_relay_modbus_init(adapter_relay_modbus_t *self,
                              ao_modbus_t *ao,
                              uint8_t slave_addr,
                              int32_t timeout_ms)
{
    if (self == NULL || ao == NULL || slave_addr == 0U || timeout_ms <= 0) {
        return -EINVAL;
    }

    (void) memset(self, 0, sizeof(*self));

    self->ao = ao;
    self->slave_addr = slave_addr;
    self->timeout_ms = timeout_ms;

    /* Default all relays to open until the first poll reconciles. */
    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        self->cache[i] = RELAY_STATE_OPEN;
    }

    return 0;
}

int adapter_relay_modbus_rebind(adapter_relay_modbus_t *self,
                                ao_modbus_t *ao,
                                uint8_t slave_addr,
                                int32_t timeout_ms)
{
    if (self == NULL || ao == NULL || ao->queue == NULL ||
        !modbus_slave_addr_is_valid(slave_addr) || timeout_ms <= 0) {
        return -EINVAL;
    }

    self->ao = ao;
    self->slave_addr = slave_addr;
    self->timeout_ms = timeout_ms;
    return 0;
}

int adapter_relay_modbus_port(adapter_relay_modbus_t *self, i_relay_t *out)
{
    if (self == NULL || out == NULL) {
        return -EINVAL;
    }

    out->api = &k_adapter_api;
    out->impl = self;

    return 0;
}

int adapter_relay_modbus_update_coils(adapter_relay_modbus_t *self,
                                      const relay_state_t *states,
                                      size_t count)
{
    if (self == NULL || states == NULL || count == 0U ||
        count > WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    for (size_t i = 0U; i < count; i++) {
        self->cache[i] = states[i];
    }
    self->cache_valid = true;

    return 0;
}

int adapter_relay_modbus_update_from_raw(adapter_relay_modbus_t *self,
                                         const uint8_t *data,
                                         size_t data_len)
{
    if (self == NULL || data == NULL || data_len < WAVESHARE_RELAY_COIL_BYTES) {
        return -EINVAL;
    }

    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];
    int err = waveshare_relay_parse_coils_response(data, data_len, states);
    if (err != 0) {
        return err;
    }

    return adapter_relay_modbus_update_coils(self, states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
}
