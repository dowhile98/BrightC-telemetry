/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure polling-schedule and health policy for Modbus slaves.
 * No Zephyr headers, no state: every function takes the descriptor and the
 * runtime record explicitly, so the policy is fully unit-testable on a host.
 */

#ifndef APP_DOMAIN_SERVICES_SLAVE_SCHEDULER_H_
#define APP_DOMAIN_SERVICES_SLAVE_SCHEDULER_H_

#include <stdbool.h>
#include <stdint.h>

#include "common/modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Interval that must elapse before the next poll of a slave.
 *
 * Returns the configured poll interval for healthy and degraded slaves, and the
 * slow backoff interval once the slave has reached
 * @ref MODBUS_HEALTH_OFFLINE_BACKOFF.
 *
 * @param[in] desc    Slave descriptor (may be NULL).
 * @param[in] runtime Runtime record (may be NULL).
 * @return Interval in milliseconds, or 0 when the arguments are unusable.
 */
uint32_t slave_scheduler_interval_ms(const modbus_slave_descriptor_t *desc,
                                     const modbus_slave_runtime_state_t *runtime);

/**
 * @brief Decide whether a slave is due for polling right now.
 *
 * A slave is due when it is enabled, it is not currently claimed by another
 * thread (or its claim is stale), and the applicable interval has elapsed since
 * the last completed transaction. A slave that has never been polled is due
 * immediately.
 *
 * @param[in] desc    Slave descriptor (may be NULL).
 * @param[in] runtime Runtime record (may be NULL).
 * @param[in] now_ms  Monotonic time in milliseconds.
 * @return true when the caller may claim the slave.
 */
bool slave_scheduler_is_due(const modbus_slave_descriptor_t *desc,
                            const modbus_slave_runtime_state_t *runtime, uint64_t now_ms);

/**
 * @brief Fold the outcome of one transaction into the runtime record.
 *
 * Also releases the in-flight claim. A Modbus exception response leaves the
 * slave healthy because the device answered, while a CRC error counts as a
 * communication failure just like a timeout.
 *
 * @param[in,out] runtime Runtime record to update (may be NULL).
 * @param[in]     result  Transaction outcome.
 * @param[in]     now_ms  Monotonic time in milliseconds.
 */
void slave_scheduler_record(modbus_slave_runtime_state_t *runtime,
                            modbus_transaction_result_t result, uint64_t now_ms);

/**
 * @brief Clear a runtime record to its power-on state.
 *
 * @param[in,out] runtime Runtime record to reset (may be NULL).
 */
void slave_scheduler_reset(modbus_slave_runtime_state_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_SLAVE_SCHEDULER_H_ */
