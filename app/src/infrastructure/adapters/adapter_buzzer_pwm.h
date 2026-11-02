/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_buzzer_pwm.h
 * @brief Infrastructure adapter for Buzzer using Zephyr PWM driver.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BUZZER_PWM_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BUZZER_PWM_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/pwm.h>

#include "common/indicator_types.h"
#include "interfaces/i_buzzer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context structure for the PWM Buzzer adapter.
 */
typedef struct {
    struct pwm_dt_spec pwm_spec;             /**< Devicetree PWM specification */
    buzzer_pattern_t   current_pattern;      /**< Last applied acoustic pattern */
    uint32_t           current_frequency_hz; /**< Last active tone frequency */
    bool               is_ready;             /**< Initialization flag */
} adapter_buzzer_pwm_t;

/**
 * @brief Initialize the PWM Buzzer adapter.
 *
 * Checks device readiness and silences the buzzer with 0 duty cycle.
 *
 * @param[out] self      Adapter instance.
 * @param[in]  pwm_spec  PWM devicetree specification.
 * @return 0 on success, -ENODEV if device is not ready, -EINVAL on bad arguments,
 *         or negative errno from PWM configuration.
 */
int adapter_buzzer_pwm_init(adapter_buzzer_pwm_t *self, const struct pwm_dt_spec *pwm_spec);

/**
 * @brief Bind the initialized adapter to an i_buzzer_t port handle.
 *
 * @param[in]  self      Adapter instance.
 * @param[out] out_port  Port handle to populate.
 * @return 0 on success, -EINVAL if arguments are invalid or adapter is uninitialized.
 */
int adapter_buzzer_pwm_port(adapter_buzzer_pwm_t *self, i_buzzer_t *out_port);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BUZZER_PWM_H_ */
