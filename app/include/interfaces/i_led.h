/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_led.h
 * @brief Discrete LED output port interface.
 *
 * Provides hardware abstraction for visual indicator output following the
 * Ports and Adapters architectural pattern.
 */

#ifndef APP_INTERFACES_I_LED_H_
#define APP_INTERFACES_I_LED_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/indicator_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * V-TABLE (implementation detail)
 *============================================================================*/

/**
 * @brief Operations exposed by a status LED adapter implementation.
 *
 * Application code reaches the LED only through the `i_led_*()` wrappers.
 * Direct access to the v-table is restricted to the infrastructure layer.
 */
typedef struct {
    /**
     * @brief Set the output state of the LED.
     *
     * @param[in] impl   Implementation context.
     * @param[in] state  Desired visual state.
     * @return 0 on success, negative errno on failure.
     */
    int (*set_state)(void *impl, led_state_t state);

    /**
     * @brief Read the current visual state of the LED.
     *
     * @param[in]  impl       Implementation context.
     * @param[out] out_state  Pointer to receive observed state.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_state)(void *impl, led_state_t *out_state);

    /**
     * @brief Configure custom blinking timing parameters.
     *
     * @param[in] impl                Implementation context.
     * @param[in] period_ms           Total blink period in milliseconds (> 0).
     * @param[in] duty_cycle_pct      ON time percentage (0..100).
     * @return 0 on success, negative errno on failure.
     */
    int (*set_blink_rate)(void *impl, uint32_t period_ms, uint32_t duty_cycle_pct);
} i_led_api_t;

/**
 * @brief Polymorphic LED handle: v-table plus opaque implementation pointer.
 */
typedef struct {
    const i_led_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;             /**< Concrete adapter instance */
} i_led_t;

/*============================================================================*
 * VALIDATED INLINE WRAPPERS
 *============================================================================*/

/**
 * @brief Check that an LED handle is bound to an implementation.
 *
 * @param[in] led Handle to test (may be NULL).
 * @return true when both the vtable and implementation are non-NULL.
 */
static inline bool i_led_is_bound(const i_led_t *led)
{
    return (led != NULL) &&
           (led->api != NULL) &&
           (led->impl != NULL) &&
           (led->api->set_state != NULL) &&
           (led->api->get_state != NULL) &&
           (led->api->set_blink_rate != NULL);
}

/**
 * @brief Set the state of the LED.
 *
 * @param[in] led    LED handle.
 * @param[in] state  Desired state.
 * @return 0 on success, -EINVAL on unbound handle or invalid state, or adapter error.
 */
static inline int i_led_set_state(const i_led_t *led, led_state_t state)
{
    if (!i_led_is_bound(led) || !led_state_is_valid(state)) {
        return -EINVAL;
    }
    return led->api->set_state(led->impl, state);
}

/**
 * @brief Read the current state of the LED.
 *
 * @param[in]  led        LED handle.
 * @param[out] out_state  Pointer to store observed state.
 * @return 0 on success, -EINVAL on invalid arguments, or adapter error.
 */
static inline int i_led_get_state(const i_led_t *led, led_state_t *out_state)
{
    if (!i_led_is_bound(led) || (out_state == NULL)) {
        return -EINVAL;
    }
    return led->api->get_state(led->impl, out_state);
}

/**
 * @brief Configure blink timing parameters.
 *
 * @param[in] led             LED handle.
 * @param[in] period_ms       Blink period in ms (> 0).
 * @param[in] duty_cycle_pct  Duty cycle percentage (0..100).
 * @return 0 on success, -EINVAL on invalid arguments, or adapter error.
 */
static inline int i_led_set_blink_rate(const i_led_t *led, uint32_t period_ms, uint32_t duty_cycle_pct)
{
    if (!i_led_is_bound(led) || (period_ms == 0U) || (duty_cycle_pct > 100U)) {
        return -EINVAL;
    }
    return led->api->set_blink_rate(led->impl, period_ms, duty_cycle_pct);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_LED_H_ */
