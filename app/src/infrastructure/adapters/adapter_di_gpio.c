/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_di_gpio.c
 * @brief Infrastructure adapter for Digital Inputs using Zephyr GPIO and SPI expanders.
 */

#include "infrastructure/adapters/adapter_di_gpio.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_di_gpio, LOG_LEVEL_INF);

int adapter_di_gpio_init(adapter_di_gpio_t *self,
                         const struct device *mcp23s17_0,
                         const struct device *mcp23s17_1)
{
    if (self == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));

    static const struct gpio_dt_spec s_default_onchip_gpios[ONCHIP_DI_COUNT] = {
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
        [ONCHIP_DI_USER_BUTTON] = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
#elif DT_NODE_EXISTS(DT_NODELABEL(user_button))
        [ONCHIP_DI_USER_BUTTON] = GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios),
#else
        [ONCHIP_DI_USER_BUTTON] = {0},
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(sw_bypass))
        [ONCHIP_DI_SW_BYPASS] = GPIO_DT_SPEC_GET(DT_NODELABEL(sw_bypass), gpios),
#else
        [ONCHIP_DI_SW_BYPASS] = {0},
#endif
    };

    memcpy(self->onchip_gpios, s_default_onchip_gpios, sizeof(self->onchip_gpios));

    self->mcp23s17[0] = mcp23s17_0;
    self->mcp23s17[1] = mcp23s17_1;

    for (size_t i = 0U; i < (size_t)ONCHIP_DI_COUNT; i++) {
        if (self->onchip_gpios[i].port != NULL && gpio_is_ready_dt(&self->onchip_gpios[i])) {
            int ret = gpio_pin_configure_dt(&self->onchip_gpios[i], GPIO_INPUT);
            if (ret < 0) {
                LOG_WRN("onchip_gpio[%zu] config failed: %d", i, ret);
            }
        }
    }

    for (size_t d = 0U; d < 2U; d++) {
        if (self->mcp23s17[d] != NULL && device_is_ready(self->mcp23s17[d])) {
            if (d == 0U) {
                for (gpio_pin_t pin = 0U; pin < 8U; pin++) {
                    int ret = gpio_pin_configure(self->mcp23s17[d], pin, GPIO_INPUT | GPIO_PULL_UP);
                    if (ret < 0) {
                        LOG_WRN("mcp23s17_%zu pin %u config failed: %d", d, pin, ret);
                    }
                }
            }
        }
    }

    self->is_ready = true;
    return 0;
}

int adapter_di_gpio_read(void *ctx,
                         di_source_type_t source,
                         uint8_t dev_id,
                         uint8_t pin_idx,
                         bool *out_raw)
{
    adapter_di_gpio_t *self = (adapter_di_gpio_t *)ctx;

    if (self == NULL || out_raw == NULL) {
        return -EINVAL;
    }

    if (!self->is_ready) {
        return -EINVAL;
    }

    if (source == DI_SOURCE_ONCHIP_GPIO) {
        if (dev_id >= (uint8_t)ONCHIP_DI_COUNT) {
            return -EINVAL;
        }
        const struct gpio_dt_spec *spec = &self->onchip_gpios[dev_id];
        if (spec->port == NULL || !gpio_is_ready_dt(spec)) {
            return -ENODEV;
        }
        int val = gpio_pin_get_dt(spec);
        if (val < 0) {
            return val;
        }
        *out_raw = (val > 0);
        return 0;
    }

    if (source == DI_SOURCE_INTERNAL_SPI) {
        if (dev_id >= 2U) {
            return -EINVAL;
        }
        const struct device *dev = self->mcp23s17[dev_id];
        if (dev == NULL || !device_is_ready(dev)) {
            return -ENODEV;
        }
        int val = gpio_pin_get(dev, (gpio_pin_t)pin_idx);
        if (val < 0) {
            return val;
        }
        *out_raw = (val > 0);
        return 0;
    }

    return -ENOTSUP;
}
