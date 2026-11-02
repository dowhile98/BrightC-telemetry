/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_led_gpio.h
 * @brief Infrastructure adapter for Status LED using Zephyr GPIO driver.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_LED_GPIO_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_LED_GPIO_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>

#include "common/indicator_types.h"
#include "interfaces/i_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context structure for the GPIO LED adapter.
 */
typedef struct {
    struct gpio_dt_spec gpio_spec;            /**< Devicetree GPIO specification */
    led_state_t         current_state;        /**< Last applied LED visual state */
    uint32_t            blink_period_ms;      /**< Blink period in ms */
    uint32_t            blink_duty_cycle_pct; /**< Blink duty cycle percentage */
    bool                is_ready;             /**< Initialization flag */
} adapter_led_gpio_t;

/**
 * @brief Initialize the GPIO LED adapter.
 *
 * Checks device readiness and configures the GPIO pin as inactive output.
 *
 * @param[out] self       Adapter instance.
 * @param[in]  gpio_spec  GPIO devicetree specification.
 * @return 0 on success, -ENODEV if device is not ready, -EINVAL on bad arguments,
 *         or negative errno from driver configuration.
 */
int adapter_led_gpio_init(adapter_led_gpio_t *self, const struct gpio_dt_spec *gpio_spec);

/**
 * @brief Bind the initialized adapter to an i_led_t port handle.
 *
 * @param[in]  self      Adapter instance.
 * @param[out] out_port  Port handle to populate.
 * @return 0 on success, -EINVAL if arguments are invalid or adapter is uninitialized.
 */
int adapter_led_gpio_port(adapter_led_gpio_t *self, i_led_t *out_port);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_LED_GPIO_H_ */
