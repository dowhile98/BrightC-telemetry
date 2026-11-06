/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_relay_gpio.c
 * @brief Concrete adapter implementing i_relay_t for internal onboard relays on BrightC V3.
 */

#include "infrastructure/adapters/adapter_relay_gpio.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if !defined(CONFIG_ZTEST)
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#endif

LOG_MODULE_REGISTER(adapter_relay_gpio, LOG_LEVEL_INF);

#if !defined(CONFIG_ZTEST)
typedef struct {
    uint8_t dev_idx; /**< 0: mcp23s17_hvac (mcp23s17_2), 1: mcp23s17_alarm (mcp23s17_3) */
    uint8_t pin;     /**< MCP23S17 pin index (0..15) */
} relay_pin_map_t;

static const relay_pin_map_t s_relay_map[ADAPTER_RELAY_GPIO_CHANNEL_COUNT] = {
    [RELAY_CH_AC1_AA]      = { .dev_idx = 0U, .pin = 9U },
    [RELAY_CH_AC1_SM]      = { .dev_idx = 0U, .pin = 10U },
    [RELAY_CH_AC1_G]       = { .dev_idx = 0U, .pin = 8U },

    [RELAY_CH_AC2_AA]      = { .dev_idx = 0U, .pin = 12U },
    [RELAY_CH_AC2_SM]      = { .dev_idx = 0U, .pin = 13U },
    [RELAY_CH_AC2_G]       = { .dev_idx = 0U, .pin = 11U },

    [RELAY_CH_AC3_AA]      = { .dev_idx = 0U, .pin = 15U },
    [RELAY_CH_AC3_SM]      = { .dev_idx = 0U, .pin = 0U },
    [RELAY_CH_AC3_G]       = { .dev_idx = 0U, .pin = 14U },

    [RELAY_CH_AC4_AA]      = { .dev_idx = 0U, .pin = 2U },
    [RELAY_CH_AC4_SM]      = { .dev_idx = 0U, .pin = 3U },
    [RELAY_CH_AC4_G]       = { .dev_idx = 0U, .pin = 1U },

    [RELAY_CH_ALARM_OUT_1] = { .dev_idx = 1U, .pin = 8U },
    [RELAY_CH_ALARM_OUT_2] = { .dev_idx = 1U, .pin = 9U },
    [RELAY_CH_ALARM_OUT_3] = { .dev_idx = 1U, .pin = 10U },
    [RELAY_CH_ALARM_OUT_4] = { .dev_idx = 1U, .pin = 11U },
    [RELAY_CH_ALARM_OUT_5] = { .dev_idx = 1U, .pin = 12U },
    [RELAY_CH_ALARM_OUT_6] = { .dev_idx = 1U, .pin = 13U },
};
#endif

static int adapter_relay_gpio_impl_set_state(void *impl, relay_channel_t channel,
                                            relay_state_t state)
{
    adapter_relay_gpio_t *self = (adapter_relay_gpio_t *)impl;

    if (self == NULL || !self->is_ready) {
        return -EINVAL;
    }
    if ((uint32_t)channel >= ADAPTER_RELAY_GPIO_CHANNEL_COUNT) {
        return -EINVAL;
    }
    if (state != RELAY_STATE_OPEN && state != RELAY_STATE_CLOSED) {
        return -EINVAL;
    }

#if !defined(CONFIG_ZTEST)
    const relay_pin_map_t *map = &s_relay_map[channel];
    const struct device *dev = (map->dev_idx == 0U) ? self->mcp23s17_hvac : self->mcp23s17_alarm;

    if (dev == NULL || !device_is_ready(dev)) {
        self->is_fault[channel] = true;
        return -ENODEV;
    }

    int pin_val = (state == RELAY_STATE_CLOSED) ? 1 : 0;
    int err = gpio_pin_set(dev, (gpio_pin_t)map->pin, pin_val);
    if (err != 0) {
        LOG_ERR("Failed to set relay ch %u pin %u: %d", (uint32_t)channel, map->pin, err);
        self->is_fault[channel] = true;
        return err;
    }
#endif

    if (self->states[channel] != state) {
        self->cycle_counts[channel]++;
        self->last_toggle_ms[channel] = k_uptime_get_32();
    }
    self->states[channel] = state;
    self->is_fault[channel] = false;
    return 0;
}

static int adapter_relay_gpio_impl_get_state(void *impl, relay_channel_t channel,
                                            relay_state_t *out_state)
{
    adapter_relay_gpio_t *self = (adapter_relay_gpio_t *)impl;

    if (self == NULL || !self->is_ready || out_state == NULL) {
        return -EINVAL;
    }
    if ((uint32_t)channel >= ADAPTER_RELAY_GPIO_CHANNEL_COUNT) {
        return -EINVAL;
    }

    *out_state = self->states[channel];
    return 0;
}

static int adapter_relay_gpio_impl_get_status(void *impl, relay_channel_t channel,
                                             relay_channel_status_t *out_status)
{
    adapter_relay_gpio_t *self = (adapter_relay_gpio_t *)impl;

    if (self == NULL || !self->is_ready || out_status == NULL) {
        return -EINVAL;
    }
    if ((uint32_t)channel >= ADAPTER_RELAY_GPIO_CHANNEL_COUNT) {
        return -EINVAL;
    }

    out_status->state = self->states[channel];
    out_status->cycle_count = self->cycle_counts[channel];
    out_status->last_toggle_ms = self->last_toggle_ms[channel];
    out_status->is_fault = self->is_fault[channel];
    return 0;
}

static const i_relay_api_t s_adapter_relay_gpio_api = {
    .set_state = adapter_relay_gpio_impl_set_state,
    .get_state = adapter_relay_gpio_impl_get_state,
    .get_status = adapter_relay_gpio_impl_get_status,
};

int adapter_relay_gpio_init(adapter_relay_gpio_t *self,
                            const struct device *mcp23s17_hvac,
                            const struct device *mcp23s17_alarm)
{
    if (self == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->mcp23s17_hvac = mcp23s17_hvac;
    self->mcp23s17_alarm = mcp23s17_alarm;

    for (size_t i = 0U; i < ADAPTER_RELAY_GPIO_CHANNEL_COUNT; i++) {
        self->states[i] = RELAY_STATE_OPEN;
        self->cycle_counts[i] = 0U;
        self->last_toggle_ms[i] = 0U;
        self->is_fault[i] = false;
    }

#if !defined(CONFIG_ZTEST)
    for (size_t i = 0U; i < ADAPTER_RELAY_GPIO_CHANNEL_COUNT; i++) {
        const relay_pin_map_t *map = &s_relay_map[i];
        const struct device *dev = (map->dev_idx == 0U) ? self->mcp23s17_hvac : self->mcp23s17_alarm;

        if (dev != NULL && device_is_ready(dev)) {
            int ret = gpio_pin_configure(dev, (gpio_pin_t)map->pin, GPIO_OUTPUT_INACTIVE);
            if (ret != 0) {
                LOG_WRN("Relay ch %zu (dev %u pin %u) config failed: %d",
                        i, map->dev_idx, map->pin, ret);
                self->is_fault[i] = true;
            }
        } else {
            self->is_fault[i] = true;
        }
    }
#endif

    self->is_ready = true;
    return 0;
}

int adapter_relay_gpio_port(adapter_relay_gpio_t *self, i_relay_t *out_port)
{
    if (self == NULL || !self->is_ready || out_port == NULL) {
        return -EINVAL;
    }

    out_port->api = &s_adapter_relay_gpio_api;
    out_port->impl = self;
    return 0;
}

i_relay_t adapter_relay_gpio_get_port(adapter_relay_gpio_t *self)
{
    i_relay_t port = {
        .api = (self != NULL && self->is_ready) ? &s_adapter_relay_gpio_api : NULL,
        .impl = self,
    };
    return port;
}
