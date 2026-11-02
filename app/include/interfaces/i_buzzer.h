/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_buzzer.h
 * @brief Discrete Buzzer output port interface.
 *
 * Provides hardware abstraction for acoustic indicators and tone generation
 * following the Ports and Adapters architectural pattern.
 */

#ifndef APP_INTERFACES_I_BUZZER_H_
#define APP_INTERFACES_I_BUZZER_H_

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
 * @brief Operations exposed by a buzzer adapter implementation.
 *
 * Application code reaches the buzzer only through the `i_buzzer_*()` wrappers.
 * Direct access to the v-table is restricted to the infrastructure layer.
 */
typedef struct {
    /**
     * @brief Play a single tone at a specific frequency for a given duration.
     *
     * @param[in] impl          Implementation context.
     * @param[in] frequency_hz  Frequency in Hertz (0 to stop).
     * @param[in] duration_ms   Tone duration in milliseconds (0 = continuous).
     * @return 0 on success, negative errno on failure.
     */
    int (*play_tone)(void *impl, uint32_t frequency_hz, uint32_t duration_ms);

    /**
     * @brief Play a predefined buzzer acoustic pattern.
     *
     * @param[in] impl          Implementation context.
     * @param[in] pattern       Acoustic pattern to activate.
     * @param[in] frequency_hz  Base frequency in Hertz for the pattern.
     * @return 0 on success, negative errno on failure.
     */
    int (*play_pattern)(void *impl, buzzer_pattern_t pattern, uint32_t frequency_hz);

    /**
     * @brief Stop buzzer output immediately and silence device.
     *
     * @param[in] impl Implementation context.
     * @return 0 on success, negative errno on failure.
     */
    int (*stop)(void *impl);

    /**
     * @brief Read the currently active buzzer pattern.
     *
     * @param[in]  impl         Implementation context.
     * @param[out] out_pattern  Pointer to store active pattern.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_active_pattern)(void *impl, buzzer_pattern_t *out_pattern);
} i_buzzer_api_t;

/**
 * @brief Polymorphic buzzer handle: v-table plus opaque implementation pointer.
 */
typedef struct {
    const i_buzzer_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;                /**< Concrete adapter instance */
} i_buzzer_t;

/*============================================================================*
 * VALIDATED INLINE WRAPPERS
 *============================================================================*/

/**
 * @brief Check that a buzzer handle is bound to an implementation.
 *
 * @param[in] buzzer Handle to test (may be NULL).
 * @return true when both the vtable and implementation are non-NULL.
 */
static inline bool i_buzzer_is_bound(const i_buzzer_t *buzzer)
{
    return (buzzer != NULL) &&
           (buzzer->api != NULL) &&
           (buzzer->impl != NULL) &&
           (buzzer->api->play_tone != NULL) &&
           (buzzer->api->play_pattern != NULL) &&
           (buzzer->api->stop != NULL) &&
           (buzzer->api->get_active_pattern != NULL);
}

/**
 * @brief Play a single tone on the buzzer.
 *
 * @param[in] buzzer        Buzzer handle.
 * @param[in] frequency_hz  Frequency in Hz.
 * @param[in] duration_ms   Duration in ms (0 = continuous).
 * @return 0 on success, -EINVAL on unbound handle, or adapter error.
 */
static inline int i_buzzer_play_tone(const i_buzzer_t *buzzer, uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!i_buzzer_is_bound(buzzer)) {
        return -EINVAL;
    }
    return buzzer->api->play_tone(buzzer->impl, frequency_hz, duration_ms);
}

/**
 * @brief Play an acoustic pattern on the buzzer.
 *
 * @param[in] buzzer        Buzzer handle.
 * @param[in] pattern       Pattern to play.
 * @param[in] frequency_hz  Base tone frequency in Hz.
 * @return 0 on success, -EINVAL on invalid arguments, or adapter error.
 */
static inline int i_buzzer_play_pattern(const i_buzzer_t *buzzer, buzzer_pattern_t pattern, uint32_t frequency_hz)
{
    if (!i_buzzer_is_bound(buzzer) || !buzzer_pattern_is_valid(pattern)) {
        return -EINVAL;
    }
    return buzzer->api->play_pattern(buzzer->impl, pattern, frequency_hz);
}

/**
 * @brief Stop any active buzzer tone or pattern.
 *
 * @param[in] buzzer Buzzer handle.
 * @return 0 on success, -EINVAL on unbound handle, or adapter error.
 */
static inline int i_buzzer_stop(const i_buzzer_t *buzzer)
{
    if (!i_buzzer_is_bound(buzzer)) {
        return -EINVAL;
    }
    return buzzer->api->stop(buzzer->impl);
}

/**
 * @brief Query the active buzzer pattern.
 *
 * @param[in]  buzzer       Buzzer handle.
 * @param[out] out_pattern  Pointer to store observed pattern.
 * @return 0 on success, -EINVAL on invalid arguments, or adapter error.
 */
static inline int i_buzzer_get_active_pattern(const i_buzzer_t *buzzer, buzzer_pattern_t *out_pattern)
{
    if (!i_buzzer_is_bound(buzzer) || (out_pattern == NULL)) {
        return -EINVAL;
    }
    return buzzer->api->get_active_pattern(buzzer->impl, out_pattern);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_BUZZER_H_ */
