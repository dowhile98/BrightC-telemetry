/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "slave_scheduler.h"

#include <stddef.h>
#include <string.h>

/** Timestamp helper: only move a stored timestamp forward. */
static uint64_t advance_timestamp(uint64_t current, uint64_t candidate, bool has_previous)
{
    if (!has_previous || candidate >= current) {
        return candidate;
    }

    return current;
}

/** Bump a counter without wrapping around to zero. */
static void increment_saturating(uint32_t *counter)
{
    if (*counter < UINT32_MAX) {
        (*counter)++;
    }
}

uint32_t slave_scheduler_interval_ms(const modbus_slave_descriptor_t *desc,
                                     const modbus_slave_runtime_state_t *runtime)
{
    if (desc == NULL || runtime == NULL) {
        return 0U;
    }

    if (runtime->health == MODBUS_HEALTH_OFFLINE_BACKOFF) {
        return MODBUS_OFFLINE_BACKOFF_INTERVAL_MS;
    }

    return desc->poll_interval_ms;
}

bool slave_scheduler_is_due(const modbus_slave_descriptor_t *desc,
                            const modbus_slave_runtime_state_t *runtime, uint64_t now_ms)
{
    if (desc == NULL || runtime == NULL || !desc->is_enabled) {
        return false;
    }

    if (runtime->in_flight) {
        /* Unsigned subtraction: correct across a counter wrap. */
        uint64_t claimed_for_ms = now_ms - runtime->last_attempt_ms;

        if (claimed_for_ms < MODBUS_CLAIM_STALE_MS) {
            return false;
        }
    }

    if (!runtime->has_polled) {
        return true;
    }

    uint32_t interval_ms = slave_scheduler_interval_ms(desc, runtime);

    return (now_ms - runtime->last_poll_timestamp_ms) >= (uint64_t) interval_ms;
}

void slave_scheduler_record(modbus_slave_runtime_state_t *runtime,
                            modbus_transaction_result_t result, uint64_t now_ms)
{
    if (runtime == NULL) {
        return;
    }

    bool had_poll = runtime->has_polled;

    runtime->in_flight = false;

    if (result == MODBUS_RESULT_INVALID) {
        /* Nothing reached the wire, so the health record is untouched.
         * The schedule still has to advance, though: the rejection is
         * deterministic, and retrying it immediately would spin the polling
         * thread at full speed instead of once per interval. */
        runtime->last_poll_timestamp_ms =
            advance_timestamp(runtime->last_poll_timestamp_ms, now_ms, had_poll);
        runtime->has_polled = true;
        return;
    }

    runtime->last_poll_timestamp_ms = advance_timestamp(runtime->last_poll_timestamp_ms, now_ms,
                                                        had_poll);

    switch (result) {
    case MODBUS_RESULT_OK:
        increment_saturating(&runtime->successful_polls);
        runtime->consecutive_fails = 0U;
        runtime->health = MODBUS_HEALTH_ONLINE;
        runtime->last_success_timestamp_ms =
            advance_timestamp(runtime->last_success_timestamp_ms, now_ms, had_poll);
        break;

    case MODBUS_RESULT_TIMEOUT:
        increment_saturating(&runtime->timeout_count);
        increment_saturating(&runtime->consecutive_fails);
        runtime->health = (runtime->consecutive_fails >= MODBUS_CONSECUTIVE_FAILS_THRESHOLD)
                              ? MODBUS_HEALTH_OFFLINE_BACKOFF
                              : MODBUS_HEALTH_DEGRADED;
        break;

    case MODBUS_RESULT_CRC:
        increment_saturating(&runtime->crc_error_count);
        increment_saturating(&runtime->consecutive_fails);
        runtime->health = (runtime->consecutive_fails >= MODBUS_CONSECUTIVE_FAILS_THRESHOLD)
                              ? MODBUS_HEALTH_OFFLINE_BACKOFF
                              : MODBUS_HEALTH_DEGRADED;
        break;

    case MODBUS_RESULT_EXCEPTION:
        /* The device answered: it is alive, the request was wrong. */
        increment_saturating(&runtime->exception_count);
        break;

    case MODBUS_RESULT_INVALID:
    default:
        break;
    }

    runtime->has_polled = true;
}

void slave_scheduler_reset(modbus_slave_runtime_state_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    (void) memset(runtime, 0, sizeof(*runtime));
    runtime->health = MODBUS_HEALTH_ONLINE;
}
