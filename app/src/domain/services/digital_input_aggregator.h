/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file digital_input_aggregator.h
 * @brief Domain aggregator service for digital inputs and contact alarms.
 *
 * Implements pure C99/C11 domain logic for channel ingestion, debounce filtering,
 * polarity normalization, active duration tracking, and event emission.
 * Zero Zephyr headers, zero dynamic memory allocation.
 */

#ifndef APP_DOMAIN_SERVICES_DIGITAL_INPUT_AGGREGATOR_H_
#define APP_DOMAIN_SERVICES_DIGITAL_INPUT_AGGREGATOR_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"
#include "interfaces/i_digital_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Raw hardware reader callback function.
 *
 * Allows the aggregator to query physical pin levels on demand during each scan cycle.
 *
 * @param[in]  ctx      User context pointer provided at initialization.
 * @param[in]  source   Physical source type (SPI expander, Modbus, etc.).
 * @param[in]  dev_id   Chip index or Modbus slave ID.
 * @param[in]  pin_idx  Pin offset (0..15) or bit index.
 * @param[out] out_raw  Destination receiving raw pin logic level.
 * @return 0 on success, negative errno on communication/read error.
 */
typedef int (*di_raw_reader_fn_t)(void *ctx,
                                  di_source_type_t source,
                                  uint8_t dev_id,
                                  uint8_t pin_idx,
                                  bool *out_raw);

/**
 * @brief Digital Input Aggregator state structure.
 *
 * Statically allocated by the application container or test harness.
 */
typedef struct {
    di_channel_config_t configs[DI_CHANNEL_MAX];
    debounce_filter_t   filters[DI_CHANNEL_MAX];
    di_channel_status_t statuses[DI_CHANNEL_MAX];
    bool                raw_inputs[DI_CHANNEL_MAX];
    uint32_t            active_accum_ms[DI_CHANNEL_MAX];
    size_t              channel_count;
    uint64_t            current_time_ms;
    di_raw_reader_fn_t  raw_reader;
    void               *reader_ctx;
} di_aggregator_t;

/**
 * @brief Initialize the digital input aggregator service.
 *
 * @param[in,out] self        Aggregator instance to initialize.
 * @param[in]     configs     Array of channel configuration descriptors.
 * @param[in]     count       Number of configurations in array (1..DI_CHANNEL_MAX).
 * @param[in]     reader      Optional raw hardware reader callback (may be NULL if driven externally).
 * @param[in]     reader_ctx  User context passed to reader callback.
 * @return 0 on success, negative errno on invalid arguments.
 */
int di_aggregator_init(di_aggregator_t *self,
                       const di_channel_config_t *configs,
                       size_t count,
                       di_raw_reader_fn_t reader,
                       void *reader_ctx);

/**
 * @brief Reconfigure channel descriptors without resetting the hardware reader.
 *
 * @param[in,out] self    Aggregator instance.
 * @param[in]     configs New channel configurations.
 * @param[in]     count   Number of channel configurations.
 * @return 0 on success, negative errno on error.
 */
int di_aggregator_reconfigure(di_aggregator_t *self,
                              const di_channel_config_t *configs,
                              size_t count);

/**
 * @brief Execute a periodic scan cycle across all configured channels.
 *
 * Dispatches hardware reads (if reader configured), normalizes polarity, updates
 * debounce filters, tracks active durations, and collects state transition events.
 *
 * @param[in,out] self            Aggregator instance.
 * @param[in]     elapsed_ms      Time elapsed since last scan in milliseconds.
 * @param[out]    out_events      Array to receive confirmed state transition events (may be NULL).
 * @param[in]     max_events      Maximum number of events out_events can hold.
 * @param[out]    out_event_count Destination receiving count of events written (may be NULL).
 * @return 0 on success, negative errno on error.
 */
int di_aggregator_scan(di_aggregator_t *self,
                       uint32_t elapsed_ms,
                       di_transition_event_t *out_events,
                       size_t max_events,
                       size_t *out_event_count);

/**
 * @brief Query debounced logical active state for a semantic role.
 *
 * @param[in]  self           Aggregator instance.
 * @param[in]  role           Semantic role to query.
 * @param[out] out_is_active  Destination receiving true if active/asserted, false if inactive.
 * @return 0 on success, -ENOENT if channel not found or disabled, -EINVAL on invalid args.
 */
int di_aggregator_get_state(const di_aggregator_t *self, di_role_t role, bool *out_is_active);

/**
 * @brief Query most recent raw physical pin level for a semantic role.
 *
 * @param[in]  self     Aggregator instance.
 * @param[in]  role     Semantic role to query.
 * @param[out] out_raw  Destination receiving physical pin logic level.
 * @return 0 on success, -ENOENT if channel not found or disabled, -EINVAL on invalid args.
 */
int di_aggregator_get_raw(const di_aggregator_t *self, di_role_t role, bool *out_raw);

/**
 * @brief Read runtime status and diagnostic metrics for a semantic role.
 *
 * @param[in]  self        Aggregator instance.
 * @param[in]  role        Semantic role to query.
 * @param[out] out_status  Destination receiving channel status structure.
 * @return 0 on success, -ENOENT if channel not found or disabled, -EINVAL on invalid args.
 */
int di_aggregator_get_status(const di_aggregator_t *self, di_role_t role, di_channel_status_t *out_status);

/**
 * @brief Compute 64-bit mask of all currently active channels indexed by role.
 *
 * Bit (1ULL << role) is set if that role is currently asserted and active.
 *
 * @param[in]  self      Aggregator instance.
 * @param[out] out_mask  Destination receiving 64-bit bitmask.
 * @return 0 on success, -EINVAL on invalid args.
 */
int di_aggregator_get_active_mask(const di_aggregator_t *self, uint64_t *out_mask);

/**
 * @brief Pack current aggregator states into a consolidated telemetry snapshot.
 *
 * @param[in]  self          Aggregator instance (must be non-NULL).
 * @param[out] out_snapshot  Destination snapshot structure (must be non-NULL).
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int di_aggregator_pack_snapshot(const di_aggregator_t *self, di_snapshot_msg_t *out_snapshot);


/**
 * @brief Manually inject a raw pin level for a specific channel index.
 *
 * Useful for unit testing or when raw inputs are updated directly by hardware interrupts.
 *
 * @param[in,out] self       Aggregator instance.
 * @param[in]     index      Channel index (0..channel_count-1).
 * @param[in]     raw_level  Raw logic level to store.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int di_aggregator_set_raw_by_index(di_aggregator_t *self, size_t index, bool raw_level);

/**
 * @brief Manually inject a raw pin level for a semantic role.
 *
 * @param[in,out] self       Aggregator instance.
 * @param[in]     role       Semantic role.
 * @param[in]     raw_level  Raw logic level to store.
 * @return 0 on success, -ENOENT if not found, -EINVAL on invalid arguments.
 */
int di_aggregator_set_raw_by_role(di_aggregator_t *self, di_role_t role, bool raw_level);

/**
 * @brief Export an i_digital_input_t port handle bound to this aggregator instance.
 *
 * @param[in,out] self Aggregator instance.
 * @return Polymorphic port handle.
 */
i_digital_input_t di_aggregator_port(di_aggregator_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_DIGITAL_INPUT_AGGREGATOR_H_ */
