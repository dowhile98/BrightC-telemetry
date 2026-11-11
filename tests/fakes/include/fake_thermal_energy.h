/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_thermal_energy.h
 * @brief In-memory Fake implementation of i_thermal_energy port for unit testing.
 *
 * Pure ISO C99/C11:
 * - Zero Zephyr RTOS dependencies
 * - Zero dynamic memory allocation
 * - Zero file-scope mutable state outside test double harness
 */

#ifndef FAKE_THERMAL_ENERGY_H_
#define FAKE_THERMAL_ENERGY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_thermal_energy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_THERMAL_ENERGY_HISTORY_DEPTH 24U

typedef struct {
    thermal_energy_result_t   result;
    thermal_energy_capture_t  capture;
    thermal_hourly_record_t   hourly_records[FAKE_THERMAL_ENERGY_HISTORY_DEPTH];
    bool                      hourly_record_present[FAKE_THERMAL_ENERGY_HISTORY_DEPTH];
    config_thermal_baseline_t baseline;

    /* Call counters */
    uint32_t get_latest_result_calls;
    uint32_t get_latest_capture_calls;
    uint32_t get_hourly_history_calls;
    uint32_t get_baseline_calls;
    uint32_t trigger_recalibration_calls;

    /* Override return codes */
    int get_latest_result_ret;
    int get_latest_capture_ret;
    int get_hourly_history_ret;
    int get_baseline_ret;
    int trigger_recalibration_ret;
} fake_thermal_energy_state_t;

/**
 * @brief Reset fake state, zeroing data, records and call counters.
 */
void fake_thermal_energy_reset(void);

/**
 * @brief Get the port handle bound to the fake adapter instance.
 */
i_thermal_energy_t fake_thermal_energy_get_port(void);

/**
 * @brief Configure the thermodynamic result snapshot to be returned by get_latest_result.
 */
void fake_thermal_energy_set_latest_result(const thermal_energy_result_t *result);

/**
 * @brief Configure the telemetry capture snapshot to be returned by get_latest_capture.
 */
void fake_thermal_energy_set_latest_capture(const thermal_energy_capture_t *capture);

/**
 * @brief Set an hourly log record at a given offset.
 */
void fake_thermal_energy_set_hourly_record(uint16_t hours_ago, const thermal_hourly_record_t *record);

/**
 * @brief Configure the steady-state baseline to be returned by get_baseline.
 */
void fake_thermal_energy_set_baseline(const config_thermal_baseline_t *baseline);

/**
 * @brief Override return code for get_latest_result.
 */
void fake_thermal_energy_set_latest_result_ret(int ret);

/**
 * @brief Override return code for get_latest_capture.
 */
void fake_thermal_energy_set_latest_capture_ret(int ret);

/**
 * @brief Override return code for get_hourly_history.
 */
void fake_thermal_energy_set_hourly_history_ret(int ret);

/**
 * @brief Override return code for get_baseline.
 */
void fake_thermal_energy_set_baseline_ret(int ret);

/**
 * @brief Override return code for trigger_baseline_recalibration.
 */
void fake_thermal_energy_set_recalibration_ret(int ret);

/**
 * @brief Inspect the internal state of the fake adapter.
 */
const fake_thermal_energy_state_t *fake_thermal_energy_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_THERMAL_ENERGY_H_ */
