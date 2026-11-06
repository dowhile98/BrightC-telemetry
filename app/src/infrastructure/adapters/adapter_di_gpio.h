/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_di_gpio.h
 * @brief Infrastructure adapter for Digital Inputs using Zephyr GPIO and SPI expanders.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_DI_GPIO_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_DI_GPIO_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/spinlock.h>

#include "common/digital_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct gpio_dt_spec onchip_gpios[ONCHIP_DI_COUNT];
    const struct device *mcp23s17[2];
    struct k_spinlock   modbus_lock;
    uint32_t            modbus_raw_inputs;
    bool                modbus_valid;
    bool                is_ready;

    int64_t             mcp23s17_cache_time_ms[2];
    uint16_t            mcp23s17_cached_value[2];
    bool                mcp23s17_cache_valid[2];
} adapter_di_gpio_t;

/**
 * @brief Explicitly refresh the 16-bit cached port values from MCP23S17 expanders in a single batched SPI read.
 *
 * @param[in,out] self Adapter instance.
 * @return 0 on success, negative errno on failure.
 */
int adapter_di_gpio_refresh(adapter_di_gpio_t *self);

/**
 * @brief Update cached external Modbus digital input bitmask (e.g. from MBSL32DI).
 *
 * Thread-safe: protected by internal spinlock.
 *
 * @param[in,out] self Adapter instance.
 * @param[in]     raw_inputs 32-bit bitmask of external inputs.
 * @param[in]     is_valid True if Modbus polling succeeded.
 */
void adapter_di_gpio_set_modbus_raw(adapter_di_gpio_t *self, uint32_t raw_inputs, bool is_valid);

/**
 * @brief Initialize the GPIO Digital Input adapter.
 *
 * Configures on-chip GPIOs (button, bypass switch) and MCP23S17 SPI expanders.
 *
 * @param[out] self Adapter instance.
 * @param[in]  mcp23s17_0 Device pointer for first MCP23S17 SPI expander (may be NULL).
 * @param[in]  mcp23s17_1 Device pointer for second MCP23S17 SPI expander (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int adapter_di_gpio_init(adapter_di_gpio_t *self,
                         const struct device *mcp23s17_0,
                         const struct device *mcp23s17_1);

/**
 * @brief Raw hardware reader function matching di_raw_reader_fn_t.
 *
 * @param[in]  ctx      Pointer to adapter_di_gpio_t instance.
 * @param[in]  source   Physical source type (DI_SOURCE_ONCHIP_GPIO or DI_SOURCE_INTERNAL_SPI).
 * @param[in]  dev_id   Device identifier or onchip enum index.
 * @param[in]  pin_idx  Pin index.
 * @param[out] out_raw  Destination for boolean raw logic level.
 * @return 0 on success, negative errno on failure.
 */
int adapter_di_gpio_read(void *ctx,
                         di_source_type_t source,
                         uint8_t dev_id,
                         uint8_t pin_idx,
                         bool *out_raw);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_DI_GPIO_H_ */
