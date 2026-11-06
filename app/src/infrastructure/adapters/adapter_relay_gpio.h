/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_relay_gpio.h
 * @brief Concrete adapter implementing i_relay_t for internal onboard relays on BrightC V3.
 *
 * Manages 18 discrete relay channels:
 * - Channels 0..11: 4 HVAC units (3 relays each: AA, SM, G) on mcp23s17_2
 * - Channels 12..17: 6 Alarm output relays on mcp23s17_3
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_GPIO_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_GPIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for Zephyr device */
struct device;

#define ADAPTER_RELAY_GPIO_CHANNEL_COUNT 18U

/* Logical Relay Channel Definitions */
#define RELAY_CH_AC1_AA       0U  /**< AC 1 Contactor (mcp23s17_2 pin 9) */
#define RELAY_CH_AC1_SM       1U  /**< AC 1 Smart/Bypass (mcp23s17_2 pin 10) */
#define RELAY_CH_AC1_G        2U  /**< AC 1 Fan (mcp23s17_2 pin 8) */

#define RELAY_CH_AC2_AA       3U  /**< AC 2 Contactor (mcp23s17_2 pin 12) */
#define RELAY_CH_AC2_SM       4U  /**< AC 2 Smart/Bypass (mcp23s17_2 pin 13) */
#define RELAY_CH_AC2_G        5U  /**< AC 2 Fan (mcp23s17_2 pin 11) */

#define RELAY_CH_AC3_AA       6U  /**< AC 3 Contactor (mcp23s17_2 pin 15) */
#define RELAY_CH_AC3_SM       7U  /**< AC 3 Smart/Bypass (mcp23s17_2 pin 0) */
#define RELAY_CH_AC3_G        8U  /**< AC 3 Fan (mcp23s17_2 pin 14) */

#define RELAY_CH_AC4_AA       9U  /**< AC 4 Contactor (mcp23s17_2 pin 2) */
#define RELAY_CH_AC4_SM       10U /**< AC 4 Smart/Bypass (mcp23s17_2 pin 3) */
#define RELAY_CH_AC4_G        11U /**< AC 4 Fan (mcp23s17_2 pin 1) */

#define RELAY_CH_ALARM_OUT_1  12U /**< Alarm Output Relay 1 (mcp23s17_3 pin 8) */
#define RELAY_CH_ALARM_OUT_2  13U /**< Alarm Output Relay 2 (mcp23s17_3 pin 9) */
#define RELAY_CH_ALARM_OUT_3  14U /**< Alarm Output Relay 3 (mcp23s17_3 pin 10) */
#define RELAY_CH_ALARM_OUT_4  15U /**< Alarm Output Relay 4 (mcp23s17_3 pin 11) */
#define RELAY_CH_ALARM_OUT_5  16U /**< Alarm Output Relay 5 (mcp23s17_3 pin 12) */
#define RELAY_CH_ALARM_OUT_6  17U /**< Alarm Output Relay 6 (mcp23s17_3 pin 13) */

/**
 * @brief Context for internal GPIO relay adapter.
 */
typedef struct {
    const struct device *mcp23s17_hvac;   /**< SPI expander 2: HVAC relays */
    const struct device *mcp23s17_alarm;  /**< SPI expander 3: Alarm output relays */
    bool is_ready;
    relay_state_t states[ADAPTER_RELAY_GPIO_CHANNEL_COUNT];
    uint32_t cycle_counts[ADAPTER_RELAY_GPIO_CHANNEL_COUNT];
    uint32_t last_toggle_ms[ADAPTER_RELAY_GPIO_CHANNEL_COUNT];
    bool is_fault[ADAPTER_RELAY_GPIO_CHANNEL_COUNT];
} adapter_relay_gpio_t;

/**
 * @brief Initialize the GPIO relay adapter.
 *
 * Configures all 18 channels as GPIO outputs in the inactive (OPEN) state.
 *
 * @param[out] self            Adapter instance to initialize.
 * @param[in]  mcp23s17_hvac   Device pointer for mcp23s17_2 (HVAC relays, channels 0..11).
 * @param[in]  mcp23s17_alarm  Device pointer for mcp23s17_3 (Alarm relays, channels 12..17).
 * @return 0 on success, negative errno on failure.
 */
int adapter_relay_gpio_init(adapter_relay_gpio_t *self,
                            const struct device *mcp23s17_hvac,
                            const struct device *mcp23s17_alarm);

/**
 * @brief Expose the adapter through the polymorphic i_relay_t port.
 *
 * @param[in]  self     Adapter instance.
 * @param[out] out_port Destination handle to bind.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int adapter_relay_gpio_port(adapter_relay_gpio_t *self, i_relay_t *out_port);

/**
 * @brief Obtain a bound i_relay_t port handle directly.
 *
 * @param[in] self Adapter instance.
 * @return Bound i_relay_t handle, or unbound handle if self is NULL or not ready.
 */
i_relay_t adapter_relay_gpio_get_port(adapter_relay_gpio_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_GPIO_H_ */
