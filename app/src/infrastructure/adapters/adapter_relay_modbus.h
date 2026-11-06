/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infrastructure adapter: Waveshare Modbus RTU Relay 32CH behind i_relay.
 * Translates i_relay_api_t calls into asynchronous Modbus coil-write events
 * posted to the owning ao_modbus polling thread (half-duplex bus safety).
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_MODBUS_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_MODBUS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application/active_objects/ao_modbus.h"
#include "common/modbus_types.h"
#include "domain/services/waveshare_relay_codec.h"
#include "interfaces/i_modbus_client.h"
#include "interfaces/i_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One Waveshare relay module controlled through a Modbus polling thread.
 *
 * The adapter does not touch the UART directly. set_state() posts a
 * WRITE_COIL command to the ao_modbus queue; the polling thread executes it
 * on the bus it owns, preserving half-duplex safety.
 */
typedef struct {
    ao_modbus_t *ao;                 /**< Polling thread that owns the bus */
    uint8_t slave_addr;              /**< Modbus unit identifier of the module */
    int32_t timeout_ms;              /**< Per-transaction timeout */
    relay_state_t cache[WAVESHARE_RELAY_32CH_CHANNEL_COUNT]; /**< Last known state */
    bool cache_valid;                /**< True after at least one successful poll */
    uint32_t cycle_count[RELAY_CHANNEL_MAX];    /**< Accumulated switching cycle count */
    uint32_t last_toggle_ms[RELAY_CHANNEL_MAX]; /**< System uptime (ms) of last actuation */
    bool is_fault[RELAY_CHANNEL_MAX];           /**< Fault flag per channel */
} adapter_relay_modbus_t;

/**
 * @brief Initialise the adapter.
 *
 * @param[out] self     Adapter instance (static storage, never copied).
 * @param[in]  ao       Polling thread that owns the bus this module is on.
 * @param[in]  slave_addr Modbus unit identifier (1..247).
 * @param[in]  timeout_ms Response timeout for write transactions.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int adapter_relay_modbus_init(adapter_relay_modbus_t *self,
                              ao_modbus_t *ao,
                              uint8_t slave_addr,
                              int32_t timeout_ms);

/**
 * @brief Expose the adapter through the relay port.
 *
 * @param[in]  self Adapter instance.
 * @param[out] out  Handle to fill.
 *
 * @return 0 on success, -EINVAL on bad arguments or an uninitialised adapter.
 */
int adapter_relay_modbus_port(adapter_relay_modbus_t *self, i_relay_t *out);

/**
 * @brief Synchronize the adapter's cached relay states.
 *
 * Typically called after a periodic FC01 read-coils transaction succeeds.
 *
 * @param[in,out] self   Adapter instance.
 * @param[in]     states Array of at least @p count relay states.
 * @param[in]     count  Number of channels to update (1..WAVESHARE_RELAY_32CH_CHANNEL_COUNT).
 *
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int adapter_relay_modbus_update_coils(adapter_relay_modbus_t *self,
                                      const relay_state_t *states,
                                      size_t count);

/**
 * @brief Synchronize the adapter's cached relay states from raw FC01 response bytes.
 *
 * @param[in,out] self     Adapter instance.
 * @param[in]     data     Raw bit-packed bytes from FC01 read-coils.
 * @param[in]     data_len Length of @p data in bytes (must be >= WAVESHARE_RELAY_COIL_BYTES).
 *
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int adapter_relay_modbus_update_from_raw(adapter_relay_modbus_t *self,
                                         const uint8_t *data,
                                         size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RELAY_MODBUS_H_ */
