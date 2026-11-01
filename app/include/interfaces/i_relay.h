/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_relay.h
 * @brief Discrete relay output port interface.
 *
 * Abstracts the physical relay backend so that the application domain never
 * depends on a particular hardware implementation.
 *
 * @note Polymorphic Design:
 * This interface (i_relay_t) serves both:
 * 1. Internal relays: onboard GPIO pins or onboard SPI/I2C expanders (e.g. MCP23S17).
 * 2. External relays: fieldbus expansion modules (e.g. Waveshare Modbus RTU 32CH).
 *
 * All operations pass through validated inline wrappers that guarantee non-NULL
 * pointers and valid channel indices before dispatching to the underlying implementation.
 */

#ifndef APP_INTERFACES_I_RELAY_H_
#define APP_INTERFACES_I_RELAY_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay output channel index.
 *
 * Channels are zero-based within a relay module (0..31).
 * Supported by both internal relay channels and external expansion modules.
 */
typedef enum {
    RELAY_CHANNEL_0 = 0,
    RELAY_CHANNEL_1,
    RELAY_CHANNEL_2,
    RELAY_CHANNEL_3,
    RELAY_CHANNEL_4,
    RELAY_CHANNEL_5,
    RELAY_CHANNEL_6,
    RELAY_CHANNEL_7,
    RELAY_CHANNEL_8,
    RELAY_CHANNEL_9,
    RELAY_CHANNEL_10,
    RELAY_CHANNEL_11,
    RELAY_CHANNEL_12,
    RELAY_CHANNEL_13,
    RELAY_CHANNEL_14,
    RELAY_CHANNEL_15,
    RELAY_CHANNEL_16,
    RELAY_CHANNEL_17,
    RELAY_CHANNEL_18,
    RELAY_CHANNEL_19,
    RELAY_CHANNEL_20,
    RELAY_CHANNEL_21,
    RELAY_CHANNEL_22,
    RELAY_CHANNEL_23,
    RELAY_CHANNEL_24,
    RELAY_CHANNEL_25,
    RELAY_CHANNEL_26,
    RELAY_CHANNEL_27,
    RELAY_CHANNEL_28,
    RELAY_CHANNEL_29,
    RELAY_CHANNEL_30,
    RELAY_CHANNEL_31,
    RELAY_CHANNEL_MAX /**< Sentinel: number of channels; not a valid channel */
} relay_channel_t;

/**
 * @brief Desired state of a relay output.
 */
typedef enum {
    RELAY_STATE_OPEN  = 0, /**< Relay contact open  (coil off) */
    RELAY_STATE_CLOSED = 1 /**< Relay contact closed (coil on)  */
} relay_state_t;

/**
 * @brief Check that a channel index is within range.
 *
 * The adapter decides whether the channel exists on its physical module;
 * this predicate only enforces the enum range.
 */
static inline bool relay_channel_is_valid(relay_channel_t channel)
{
    return (unsigned int) channel < (unsigned int) RELAY_CHANNEL_MAX;
}

/*============================================================================*
 * V-TABLE (implementation detail)
 *============================================================================*/

/**
 * @brief Operations exposed by a relay adapter implementation.
 *
 * Application code reaches the relay only through the `i_relay_*()` wrappers
 * declared below. Only an implementation (or the composition root while
 * binding one) dereferences this table.
 */
typedef struct {
    /**
     * @brief Set the state of one relay channel.
     *
     * @param[in] impl   Implementation context.
     * @param[in] channel Channel to act on.
     * @param[in] state  Desired state.
     *
     * @return 0 on success, negative errno on failure.
     */
    int (*set_state)(void *impl, relay_channel_t channel, relay_state_t state);

    /**
     * @brief Read the current state of one relay channel.
     *
     * @param[in]  impl      Implementation context.
     * @param[in]  channel   Channel to read.
     * @param[out] out_state Destination for the observed state.
     *
     * @return 0 on success, negative errno on failure.
     */
    int (*get_state)(void *impl, relay_channel_t channel, relay_state_t *out_state);
} i_relay_api_t;

/**
 * @brief Polymorphic relay handle: v-table plus opaque implementation pointer.
 *
 * Implemented by both:
 * - External fieldbus relay adapters (e.g. Waveshare Modbus RTU 32CH via adapter_relay_modbus).
 * - Internal onboard relay adapters (e.g. GPIO-driven relays or MCP23S17 expander).
 *
 * Callers must use the `i_relay_*()` inline wrappers and never dereference
 * @ref api directly. Direct access is reserved for the composition root while
 * binding an implementation.
 */
typedef struct {
    const i_relay_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;               /**< Concrete relay adapter instance */
} i_relay_t;

/**
 * @brief Check that a relay handle is bound to an implementation.
 *
 * @param[in] relay Handle to test (may be NULL).
 * @return true when both the vtable and the implementation are present.
 */
static inline bool i_relay_is_bound(const i_relay_t *relay)
{
    return relay != NULL && relay->api != NULL && relay->impl != NULL;
}

/*============================================================================*
 * VALIDATED OPERATIONS
 *============================================================================*/

/**
 * @brief Set the state of one relay channel.
 *
 * @param[in]  relay    Bound relay handle.
 * @param[in]  channel  Channel to act on.
 * @param[in]  state    Desired state.
 *
 * @return 0 on success, -EINVAL on an unbound handle, an invalid channel, or
 *         the adapter error.
 */
static inline int i_relay_set_state(const i_relay_t *relay,
                                    relay_channel_t channel, relay_state_t state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel)) {
        return -EINVAL;
    }

    return relay->api->set_state(relay->impl, channel, state);
}

/**
 * @brief Read the current state of one relay channel.
 *
 * @param[in]  relay     Bound relay handle.
 * @param[in]  channel   Channel to read.
 * @param[out] out_state Destination for the observed state; must not be NULL.
 *
 * @return 0 on success, -EINVAL on an unbound handle, an invalid channel, a
 *         NULL output, or the adapter error.
 */
static inline int i_relay_get_state(const i_relay_t *relay,
                                    relay_channel_t channel,
                                    relay_state_t *out_state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel) ||
        out_state == NULL) {
        return -EINVAL;
    }

    return relay->api->get_state(relay->impl, channel, out_state);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_RELAY_H_ */
