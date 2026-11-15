/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file telemetry_sampler.h
 * @brief Telemetry Sampler Aggregator Application Service.
 *
 * Assembles unified periodic telemetry records across HVAC, thermal, electrical,
 * and alarm subsystems, then dispatches them to the Unified Datalogger Active Object.
 * Clean Architecture (Ports & Adapters) compliant; zero file-scope mutable state.
 */

#ifndef APP_APPLICATION_SERVICES_TELEMETRY_SAMPLER_H_
#define APP_APPLICATION_SERVICES_TELEMETRY_SAMPLER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/datalogger_types.h"
#include "application/active_objects/ao_datalogger.h"
#include "interfaces/i_thermal_energy.h"
#include "interfaces/i_alarm_query.h"
#include "interfaces/i_rtc.h"
#include "interfaces/i_hvac_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_SAMPLER_DEFAULT_INTERVAL_S 600U /* 10 minutes */

/**
 * @brief Telemetry Sampler Aggregator context.
 */
typedef struct {
    ao_datalogger_t    *datalogger_ao;
    i_thermal_energy_t  thermal_port;
    i_alarm_query_t     alarm_query;
    i_rtc_t             rtc_port;
    i_hvac_status_t     hvac_port;
    uint32_t            sample_interval_s;
    int64_t             last_sample_uptime_s;
    bool                is_initialized;
} telemetry_sampler_t;

/**
 * @brief Initialize the Telemetry Sampler Aggregator service.
 *
 * @param[in,out] self       Sampler context.
 * @param[in]     ao         Pointer to initialized Datalogger Active Object.
 * @param[in]     thermal    Pointer to abstract thermal energy port (may be NULL if disabled).
 * @param[in]     alarms     Pointer to abstract alarm query port (may be NULL if disabled).
 * @param[in]     rtc        Pointer to abstract RTC port (may be NULL if unsynchronized).
 * @param[in]     hvac       Pointer to abstract HVAC status port (may be NULL if disabled).
 * @param[in]     interval_s Periodic sampling interval in seconds (0 defaults to 600s).
 * @return 0 on success, negative errno on failure.
 */
int telemetry_sampler_init(telemetry_sampler_t *self,
                           ao_datalogger_t *ao,
                           const i_thermal_energy_t *thermal,
                           const i_alarm_query_t *alarms,
                           const i_rtc_t *rtc,
                           const i_hvac_status_t *hvac,
                           uint32_t interval_s);

/**
 * @brief Perform an immediate telemetry acquisition and post to Datalogger queue.
 *
 * @param[in,out] self    Sampler context.
 * @param[out]    out_rec Optional destination to copy the assembled sample record (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int telemetry_sampler_sample_now(telemetry_sampler_t *self,
                                 datalogger_telemetry_record_t *out_rec);

/**
 * @brief Periodic timer/cadence tick handler.
 *
 * Evaluates elapsed uptime against configured sample interval and triggers
 * sampling when interval elapses.
 *
 * @param[in,out] self         Sampler context.
 * @param[in]     now_uptime_s Current monotonic uptime in seconds.
 * @return 1 if sample was taken, 0 if skipped (interval not reached), negative errno on error.
 */
int telemetry_sampler_tick(telemetry_sampler_t *self, int64_t now_uptime_s);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_SERVICES_TELEMETRY_SAMPLER_H_ */
