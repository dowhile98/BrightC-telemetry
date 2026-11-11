/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_thermal_energy.h
 * @brief Active Object coordinator for Thermal and Energy Monitoring (Revision F Baseline).
 *
 * Implements the canonical Active Object 5-phase lifecycle:
 *   ao_thermal_energy_init -> ao_thermal_energy_start -> ao_thermal_energy_stop ->
 *   ao_thermal_energy_wait_stopped -> ao_thermal_energy_deinit
 *
 * Coordinates:
 * - 1 Hz periodic sampling & normalizer ingestion
 * - 5-phase compressor temporal stabilization state machine (compressor_fsm)
 * - Moist air psychrometrics & thermodynamic cooling capacities (thermodynamics)
 * - Predictive analytics: OLS regression, mechanical degradation, filter fouling,
 *   refrigerant leaks, room hotspots, airflow bypass, moisture ingress
 * - 720h (30-day) compressed binary circular buffer in RAM
 * - Zbus publication of calculation cycle events (chan_thermal_energy_update)
 * - Safe port contract interface (i_thermal_energy_t)
 *
 * Concurrency & Memory Invariants:
 * - Zero dynamic memory allocation (malloc, k_malloc forbidden)
 * - Zero file-scope mutable state (all state in ao_thermal_energy_t)
 * - Thread stack provided from file-scope K_THREAD_STACK_DEFINE
 * - Atomic queries (< 2 us) guarded by result_mutex
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_THERMAL_ENERGY_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_THERMAL_ENERGY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "application/zbus/sensor_channels.h"
#include "application/zbus/digital_input_channels.h"
#include "application/zbus/thermal_channels.h"
#include "common/config_types.h"
#include "common/energy_types.h"
#include "common/hvac_types.h"
#include "common/thermal_energy_types.h"
#include "domain/thermal_energy/compressor_fsm.h"
#include "domain/thermal_energy/predictive_analytics.h"
#include "interfaces/i_thermal_energy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Thread stack size reserved for the Thermal Energy Active Object. */
#define AO_THERMAL_ENERGY_STACK_SIZE 4096

/** Default priority for the Thermal Energy Active Object thread. */
#define AO_THERMAL_ENERGY_PRIORITY 5

/** Default periodic evaluation cadence: 1000 ms = 1 Hz. */
#define AO_THERMAL_ENERGY_PERIOD_MS 1000U

/** Timeout to wait for the Active Object thread to exit upon stop. */
#define AO_THERMAL_ENERGY_STOP_WAIT_MS 1500U

/** Callback prototype for auto-persisting baseline updates */
typedef void (*ao_thermal_baseline_persist_cb_t)(void *user_data, const config_thermal_baseline_t *baseline);

/**
 * @brief Thermal and Energy Active Object coordinator context.
 */
typedef struct {
    /* 1. Lifecycle & Thread Synchronization */
    struct k_thread   thread;
    k_thread_stack_t *stack;
    size_t            stack_size;
    int               priority;
    struct k_sem      start_sem;
    struct k_sem      stopped_sem;
    struct k_mutex    result_mutex;
    volatile bool     is_running;
    volatile bool     stop_requested;
    volatile bool     terminate;
    bool              is_initialized;

    /* 2. Domain Engines & Managed State */
    compressor_fsm_t         comp_fsm[THERMAL_MAX_HVAC_UNITS];
    ols_model_t              ols_model;
    thermal_energy_capture_t latest_capture;
    thermal_energy_result_t  latest_result;
    thermal_hourly_record_t  history[THERMAL_HOURLY_HISTORY_CAPACITY]; /**< 720 entries (30 days) */
    uint16_t                 history_head;
    uint16_t                 history_count;
    uint32_t                 seconds_in_current_hour;

    /* 3. Configuration & Persistence References */
    const config_thermal_site_t     *site_cfg;
    config_thermal_baseline_t       *baseline_cfg;
    ao_thermal_baseline_persist_cb_t baseline_persist_cb;
    void                            *baseline_persist_user_data;
    hvac_operation_mode_t            hvac_mode;

    /* 4. Abstract Port Interface Handle */
    i_thermal_energy_t port;

    /* 5. Ingested Telemetry Caches */
    sensor_snapshot_msg_t last_sensor_snapshot;
    bool                  has_sensor_snapshot;
    bool                  sensor_snapshot_fed;
    di_snapshot_msg_t     last_di_snapshot;
    bool                  has_di_snapshot;
    bool                  di_snapshot_fed;
    energy_reading_t      latest_elec_reading;
    bool                  has_elec_reading;
    bool                  capture_injected;
    uint32_t              result_generation_id;

    /* 6. Hourly Accumulators for Integrated OLS Regression & History */
    double   hourly_accum_t_ret;
    double   hourly_accum_t_ext;
    double   hourly_accum_f_carga;
    double   hourly_accum_q_total;
    double   hourly_accum_p_elec;
    uint32_t hourly_samples_count;
} ao_thermal_energy_t;

/* ========================================================================= */
/* Canonical 5-Phase Lifecycle Functions                                     */
/* ========================================================================= */

/**
 * @brief Phase 1: Initialize the Thermal Energy Active Object.
 *
 * Sets up synchronization primitives, binds the abstract port, zeroes data,
 * and creates the worker thread in a gated state waiting on start_sem.
 *
 * @param[out] self         Active object instance to initialize.
 * @param[in]  site_cfg     Site topology configuration (or NULL for pilot default).
 * @param[in]  baseline_cfg Learned baseline configuration reference (or NULL).
 * @param[in]  mode         HVAC operational mode for contact mapping.
 * @param[in]  stack        File-scope thread stack buffer (MPU aligned).
 * @param[in]  stack_size   Stack size in bytes (must be >= AO_THERMAL_ENERGY_STACK_SIZE).
 * @param[in]  priority     Thread preemptive priority.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int ao_thermal_energy_init(ao_thermal_energy_t *self,
                           const config_thermal_site_t *site_cfg,
                           config_thermal_baseline_t *baseline_cfg,
                           hvac_operation_mode_t mode,
                           k_thread_stack_t *stack,
                           size_t stack_size,
                           int priority);

/**
 * @brief Set the HVAC operational mode for dynamic contact mapping.
 *
 * Thread-safe update of current HVAC operational mode.
 *
 * @param[in,out] self Active object instance.
 * @param[in]     mode Target HVAC operational mode.
 */
void ao_thermal_energy_set_hvac_mode(ao_thermal_energy_t *self, hvac_operation_mode_t mode);

/**
 * @brief Phase 2: Start the Thermal Energy Active Object thread loop.
 *
 * Transitions the Active Object into the running state and signals the start gate.
 *
 * @param[in,out] self Initialized active object instance.
 * @return 0 on success, -EINVAL if not initialized, -EALREADY if already running.
 */
int ao_thermal_energy_start(ao_thermal_energy_t *self);

/**
 * @brief Phase 3: Request the Active Object thread to stop.
 *
 * Sets the stop request flag and signals the thread to exit its inner loop.
 *
 * @param[in,out] self Running active object instance.
 * @return 0 on success, -EINVAL if NULL or not initialized, -EALREADY if not running.
 */
int ao_thermal_energy_stop(ao_thermal_energy_t *self);

/**
 * @brief Phase 4: Wait for the Active Object thread to cleanly stop.
 *
 * Blocks on stopped_sem until the thread terminates its inner loop or timeout expires.
 *
 * @param[in,out] self    Active object instance.
 * @param[in]     timeout Maximum time to block waiting for stop confirmation.
 * @return 0 on success, -ETIMEDOUT on timeout, -EINVAL on invalid arguments.
 */
int ao_thermal_energy_wait_stopped(ao_thermal_energy_t *self, k_timeout_t timeout);

/**
 * @brief Phase 5: Deinitialize the Active Object and abort its thread.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, -EINVAL if NULL.
 */
int ao_thermal_energy_deinit(ao_thermal_energy_t *self);

/**
 * @brief Retrieve the bound abstract port handle for this Active Object.
 *
 * @param[in] self Initialized active object instance.
 * @return Bound i_thermal_energy_t port handle, or unbound handle on NULL.
 */
i_thermal_energy_t ao_thermal_energy_get_port(ao_thermal_energy_t *self);

/* ========================================================================= */
/* Public Ingestion & Operational Execution Methods                          */
/* ========================================================================= */

/**
 * @brief Execute one single cycle of the 1 Hz calculation engine.
 *
 * Normalizes inputs, runs compressor FSMs, computes thermodynamic balances,
 * evaluates predictive analytics, updates latest_result atomically, and publishes
 * events to Zbus chan_thermal_energy_update.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_thermal_energy_step(ao_thermal_energy_t *self);

/**
 * @brief Compress the current hour into the 720h circular buffer in RAM.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_thermal_energy_record_hour(ao_thermal_energy_t *self);

/**
 * @brief Feed a raw flat sensor snapshot into the Active Object.
 *
 * @param[in,out] self     Active object instance.
 * @param[in]     snapshot Consolidated sensor snapshot.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_thermal_energy_feed_sensor_snapshot(ao_thermal_energy_t *self,
                                           const sensor_snapshot_msg_t *snapshot);

/**
 * @brief Feed a consolidated digital input snapshot into the Active Object.
 *
 * @param[in,out] self     Active object instance.
 * @param[in]     snapshot Digital input snapshot.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_thermal_energy_feed_di_snapshot(ao_thermal_energy_t *self,
                                       const di_snapshot_msg_t *snapshot);

/**
 * @brief Feed an electrical reading into the Active Object.
 *
 * @param[in,out] self    Active object instance.
 * @param[in]     reading 3-phase AC power reading.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_thermal_energy_feed_energy_reading(ao_thermal_energy_t *self,
                                          const energy_reading_t *reading);

/**
 * @brief Inject a pre-assembled capture snapshot directly (for testing).
 *
 * @param[in,out] self    Active object instance.
 * @param[in]     capture Capture snapshot to process on next step.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_thermal_energy_feed_capture(ao_thermal_energy_t *self,
                                   const thermal_energy_capture_t *capture);

/**
 * @brief Register a callback for auto-persisting updated baseline configuration.
 *
 * @param[in,out] self      Active object instance.
 * @param[in]     cb        Callback function pointer.
 * @param[in]     user_data Context pointer passed to callback.
 */
void ao_thermal_energy_set_baseline_persist_cb(ao_thermal_energy_t *self,
                                               ao_thermal_baseline_persist_cb_t cb,
                                               void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_THERMAL_ENERGY_H_ */
