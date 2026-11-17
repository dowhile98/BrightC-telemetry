/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_snmp_traps.h
 * @brief Non-blocking enterprise SNMP trap notification adapter.
 *
 * Translates plant alarm state transitions (from Zbus chan_alarm_transition)
 * into RFC-compliant SNMP traps, offloaded to the system workqueue with
 * token bucket rate limiting and Alarm Summary Trap fallback.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SNMP_TRAPS_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SNMP_TRAPS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <snmp/snmp.h>
#include <snmp/snmp_ber.h>
#include <snmp/snmp_trap.h>

#include "common/alarm_types.h"
#include "interfaces/i_alarm_query.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADAPTER_SNMP_TRAP_QUEUE_SIZE     16U
#define ADAPTER_SNMP_RATE_BURST_MAX      5U
#define ADAPTER_SNMP_RATE_REFILL_MS      1000LL  /* 1 token per second */

/**
 * @brief Context structure for the SNMP trap adapter.
 */
typedef struct {
    struct snmp_agent        *agent;
    i_alarm_query_t           alarm_query;
    struct k_work             work;
    struct k_mutex            lock;
    uint32_t                  tokens;
    int64_t                   last_refill_ms;
    bool                      summary_pending;
    alarm_transition_event_t  queue[ADAPTER_SNMP_TRAP_QUEUE_SIZE];
    uint8_t                   head;
    uint8_t                   tail;
    uint8_t                   count;
    uint32_t                  dropped_count;
    bool                      initialized;
} adapter_snmp_traps_t;

/**
 * @brief Initialize the SNMP trap adapter.
 *
 * @param[out] ctx         Adapter context.
 * @param[in]  agent       zephyr-snmp agent instance.
 * @param[in]  alarm_query Alarm query port for state inspection and summary snapshots.
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int adapter_snmp_traps_init(adapter_snmp_traps_t *ctx,
                            struct snmp_agent *agent,
                            i_alarm_query_t alarm_query);

/**
 * @brief Non-blocking ingest of an alarm transition event into the trap queue.
 *
 * Safe to call from any thread or Zbus listener callback. Offloads network
 * transmission to the system workqueue.
 *
 * @param[in] ctx Adapter context.
 * @param[in] ev  Alarm transition event.
 * @return 0 on success, -ENOSPC if queue full, -EINVAL on invalid arguments.
 */
int adapter_snmp_traps_post_event(adapter_snmp_traps_t *ctx,
                                  const alarm_transition_event_t *ev);

/**
 * @brief Dispatch an immediate test trap notification.
 *
 * @param[in] ctx Adapter context.
 * @return 0 on success, negative errno on failure.
 */
int adapter_snmp_traps_send_test(adapter_snmp_traps_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SNMP_TRAPS_H_ */
