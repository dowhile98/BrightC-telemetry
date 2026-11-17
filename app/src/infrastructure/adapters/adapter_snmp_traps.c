/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_snmp_traps.c
 * @brief Implementation of non-blocking enterprise SNMP trap notification adapter.
 */

#include "infrastructure/adapters/adapter_snmp_traps.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_snmp_traps, CONFIG_LOG_DEFAULT_LEVEL);

#define DAECGE_ENTERPRISE_ID 3134U

/* BrightC Enterprise Notification OID Base: 1.3.6.1.4.1.3134.2.3.0.<subid> */
#define SNMP_OID_TRAP_BASE 1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3, 0

/* Alarm Table Column OID Base: 1.3.6.1.4.1.3134.2.3.150.1.1 */
#define SNMP_OID_ALARM_ENTRY_BASE 1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3, 150, 1, 1

static inline uint32_t get_trap_subid(uint16_t alarm_id)
{
    return (uint32_t)alarm_id;
}

static void refill_tokens(adapter_snmp_traps_t *ctx, int64_t now)
{
    int64_t elapsed = now - ctx->last_refill_ms;
    if (elapsed >= ADAPTER_SNMP_RATE_REFILL_MS) {
        uint32_t new_tokens = (uint32_t)(elapsed / ADAPTER_SNMP_RATE_REFILL_MS);
        ctx->tokens += new_tokens;
        if (ctx->tokens > ADAPTER_SNMP_RATE_BURST_MAX) {
            ctx->tokens = ADAPTER_SNMP_RATE_BURST_MAX;
        }
        ctx->last_refill_ms += (int64_t)new_tokens * ADAPTER_SNMP_RATE_REFILL_MS;
    }
}

static void send_event_trap(adapter_snmp_traps_t *ctx, const alarm_transition_event_t *ev)
{
    struct snmp_varbind vbs[4];
    memset(vbs, 0, sizeof(vbs));

    uint32_t trap_subid = get_trap_subid(ev->alarm_id);
    struct snmp_oid trap_oid = {
        .len = 11,
        .ids = {SNMP_OID_TRAP_BASE, trap_subid}
    };

    uint32_t row_idx = (uint32_t)ev->alarm_id;

    /* 1. brightCxxTrapID: 1.3.6.1.4.1.3134.2.3.150.1.1.2.<idx> */
    struct snmp_oid oid_id = {
        .len = 14,
        .ids = {SNMP_OID_ALARM_ENTRY_BASE, 2, row_idx}
    };
    vbs[0].oid = oid_id;
    vbs[0].type = ASN1_TAG_INTEGER;
    vbs[0].val.int_val = (int32_t)ev->alarm_id;

    /* 2. brightCxxTrapState: 1.3.6.1.4.1.3134.2.3.150.1.1.3.<idx> */
    struct snmp_oid oid_state = {
        .len = 14,
        .ids = {SNMP_OID_ALARM_ENTRY_BASE, 3, row_idx}
    };
    vbs[1].oid = oid_state;
    vbs[1].type = ASN1_TAG_INTEGER;
    /* 1=alarmDisable(cleared), 2=alarmEnable(active/latched) */
    vbs[1].val.int_val = (ev->event_type == ALARM_EVENT_CLEARED) ? 1 : 2;

    /* 3. brightCxxTrapTimeStamp: 1.3.6.1.4.1.3134.2.3.150.1.1.4.<idx> */
    struct snmp_oid oid_time = {
        .len = 14,
        .ids = {SNMP_OID_ALARM_ENTRY_BASE, 4, row_idx}
    };
    vbs[2].oid = oid_time;
    vbs[2].type = SNMP_TAG_TIMETICKS;
    vbs[2].val.uint_val = (uint32_t)(ev->timestamp_ms / 10ULL);

    /* 4. brightCxxTrapDsc: 1.3.6.1.4.1.3134.2.3.150.1.1.5.<idx> */
    struct snmp_oid oid_dsc = {
        .len = 14,
        .ids = {SNMP_OID_ALARM_ENTRY_BASE, 5, row_idx}
    };
    vbs[3].oid = oid_dsc;
    vbs[3].type = ASN1_TAG_OCTET_STRING;

    alarm_descriptor_t desc;
    memset(&desc, 0, sizeof(desc));
    const char *dsc_str = "Alarm Event";
    if (i_alarm_query_is_bound(&ctx->alarm_query) &&
        i_alarm_query_get_descriptor(&ctx->alarm_query, ev->alarm_id, &desc) == 0 &&
        desc.description != NULL) {
        dsc_str = desc.description;
    }
    size_t dsc_len = strlen(dsc_str);
    if (dsc_len > sizeof(vbs[3].val.octet_str.data) - 1) {
        dsc_len = sizeof(vbs[3].val.octet_str.data) - 1;
    }
    vbs[3].val.octet_str.len = dsc_len;
    memcpy(vbs[3].val.octet_str.data, dsc_str, dsc_len);

    int rc = snmp_trap_send(ctx->agent, &trap_oid, vbs, 4, false);
    if (rc != 0) {
        LOG_WRN("snmp_trap_send failed for alarm 0x%04X (trap %u): %d", ev->alarm_id, trap_subid, rc);
    } else {
        LOG_INF("Sent SNMP Trap for alarm 0x%04X (trap %u, state=%d)",
                ev->alarm_id, trap_subid, vbs[1].val.int_val);
    }
}

static void send_summary_trap(adapter_snmp_traps_t *ctx)
{
    if (!i_alarm_query_is_bound(&ctx->alarm_query)) {
        return;
    }

    alarm_bitmask_snapshot_t snapshot;
    int err = i_alarm_query_get_bitmask_snapshot(&ctx->alarm_query, &snapshot);
    if (err != 0) {
        LOG_WRN("Failed to get bitmask snapshot for summary trap: %d", err);
        return;
    }

    struct snmp_varbind vbs[2];
    memset(vbs, 0, sizeof(vbs));

    /* 1. alarmSummaryActiveCount: 1.3.6.1.4.1.3134.2.3.150.2.0 */
    struct snmp_oid oid_cnt = {
        .len = 12,
        .ids = {1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3, 150, 2, 0}
    };
    vbs[0].oid = oid_cnt;
    vbs[0].type = SNMP_TAG_GAUGE32;
    vbs[0].val.uint_val = (uint32_t)snapshot.active_count;

    /* 2. alarmSummaryActiveMask: 1.3.6.1.4.1.3134.2.3.150.3.0 (12 bytes) */
    struct snmp_oid oid_mask = {
        .len = 12,
        .ids = {1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3, 150, 3, 0}
    };
    vbs[1].oid = oid_mask;
    vbs[1].type = ASN1_TAG_OCTET_STRING;
    vbs[1].val.octet_str.len = sizeof(snapshot.active_mask);
    memcpy(vbs[1].val.octet_str.data, snapshot.active_mask, sizeof(snapshot.active_mask));

    /* Summary Trap: 1.3.6.1.4.1.3134.2.3.0.200 */
    struct snmp_oid trap_summary_oid = {
        .len = 11,
        .ids = {SNMP_OID_TRAP_BASE, 200}
    };
    int rc = snmp_trap_send(ctx->agent, &trap_summary_oid, vbs, 2, false);
    if (rc != 0) {
        LOG_WRN("snmp_trap_send failed for summary trap: %d", rc);
    } else {
        LOG_INF("Sent SNMP Alarm Summary Trap (active_count=%u)", snapshot.active_count);
    }
}

static void adapter_snmp_traps_work_handler(struct k_work *item)
{
    adapter_snmp_traps_t *ctx = CONTAINER_OF(item, adapter_snmp_traps_t, work);
    if (ctx == NULL || !ctx->initialized || ctx->agent == NULL) {
        return;
    }

    k_mutex_lock(&ctx->lock, K_FOREVER);
    int64_t now = k_uptime_get();
    refill_tokens(ctx, now);

    while (ctx->count > 0 && ctx->tokens > 0) {
        alarm_transition_event_t ev = ctx->queue[ctx->head];
        ctx->head = (ctx->head + 1U) % ADAPTER_SNMP_TRAP_QUEUE_SIZE;
        ctx->count--;
        ctx->tokens--;

        /* Release lock during network trap dispatch */
        k_mutex_unlock(&ctx->lock);
        send_event_trap(ctx, &ev);
        k_mutex_lock(&ctx->lock, K_FOREVER);

        now = k_uptime_get();
        refill_tokens(ctx, now);
    }

    if (ctx->count > 0 && ctx->tokens == 0) {
        /* Rate limit exhausted: discard remaining queue to avoid stale bursts */
        ctx->dropped_count += ctx->count;
        ctx->head = 0U;
        ctx->tail = 0U;
        ctx->count = 0U;
        ctx->summary_pending = true;
    }

    if (ctx->summary_pending && ctx->tokens > 0) {
        ctx->tokens--;
        ctx->summary_pending = false;
        k_mutex_unlock(&ctx->lock);
        send_summary_trap(ctx);
        return;
    }

    k_mutex_unlock(&ctx->lock);
}

int adapter_snmp_traps_init(adapter_snmp_traps_t *ctx,
                            struct snmp_agent *agent,
                            i_alarm_query_t alarm_query)
{
    if (ctx == NULL || agent == NULL) {
        return -EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->agent = agent;
    ctx->alarm_query = alarm_query;
    ctx->tokens = ADAPTER_SNMP_RATE_BURST_MAX;
    ctx->last_refill_ms = k_uptime_get();
    k_mutex_init(&ctx->lock);
    k_work_init(&ctx->work, adapter_snmp_traps_work_handler);
    ctx->initialized = true;

    return 0;
}

int adapter_snmp_traps_post_event(adapter_snmp_traps_t *ctx,
                                  const alarm_transition_event_t *ev)
{
    if (ctx == NULL || !ctx->initialized || ev == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&ctx->lock, K_FOREVER);

    if (ctx->count >= ADAPTER_SNMP_TRAP_QUEUE_SIZE) {
        /* Drop oldest event to maintain freshness */
        ctx->head = (ctx->head + 1U) % ADAPTER_SNMP_TRAP_QUEUE_SIZE;
        ctx->count--;
        ctx->dropped_count++;
        ctx->summary_pending = true;
    }

    ctx->queue[ctx->tail] = *ev;
    ctx->tail = (ctx->tail + 1U) % ADAPTER_SNMP_TRAP_QUEUE_SIZE;
    ctx->count++;

    k_mutex_unlock(&ctx->lock);

    /* Submit offload work */
    (void)k_work_submit(&ctx->work);
    return 0;
}

int adapter_snmp_traps_send_test(adapter_snmp_traps_t *ctx)
{
    if (ctx == NULL || !ctx->initialized || ctx->agent == NULL) {
        return -EINVAL;
    }

    alarm_transition_event_t test_ev = {
        .alarm_id = ALARM_ID_AC1_HIGH_PRESSURE,
        .event_type = ALARM_EVENT_ACTIVATED,
        .previous_state = ALARM_STATE_INACTIVE,
        .new_state = ALARM_STATE_ACTIVE,
        .severity = ALARM_SEVERITY_CRITICAL,
        .priority = ALARM_PRIORITY_HIGH,
        .source_category = ALARM_SOURCE_DIGITAL_INPUT,
        .contributing_sources_mask = 0x01,
        .trigger_value = 1.0f,
        .timestamp_ms = (uint64_t)k_uptime_get()
    };

    return adapter_snmp_traps_post_event(ctx, &test_ev);
}
