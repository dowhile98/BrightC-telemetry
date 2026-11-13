/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Port: Modbus client bound to one physical bus.
 * Abstracts nanoMODBUS (or any other stack) so that polling threads and device
 * drivers never include <zephyr/nanomodbus/...>.
 */

#ifndef APP_INTERFACES_I_MODBUS_CLIENT_H_
#define APP_INTERFACES_I_MODBUS_CLIENT_H_

#include <errno.h>
#include <stdint.h>

#include "common/modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Operations exposed by a Modbus client implementation.
 *
 * One client instance exists per physical bus; the bus is implicit in @p impl,
 * so no bus identifier appears in these signatures.
 *
 * Every transaction reports a @ref modbus_transaction_result_t instead of an
 * errno: only the adapter knows how its stack distinguishes a timeout, a CRC
 * failure and a Modbus exception response, and the polling loop must not
 * re-derive that from a lossy error code.
 *
 * These pointers are implementation detail: application code reaches the
 * client only through the `i_modbus_client_*()` wrappers declared at the end of
 * this header. Only an implementation (or the composition root while binding
 * one) refers to this table.
 */
typedef struct {
    /**
     * @brief Read a contiguous block of holding registers.
     *
     * @param[in]  impl       Implementation.
     * @param[in]  slave_addr Modbus unit identifier (1..247).
     * @param[in]  reg_addr   First register address.
     * @param[in]  quantity   Number of registers (1..125).
     * @param[out] out        Destination buffer of @p quantity words.
     * @param[in]  timeout_ms Response timeout in milliseconds.
     *
     * @return MODBUS_RESULT_OK on success; MODBUS_RESULT_EXCEPTION when the
     *         slave answered with a Modbus exception; MODBUS_RESULT_INVALID
     *         when the request was rejected before transmission.
     */
    modbus_transaction_result_t (*read_holding_registers)(void *impl, uint8_t slave_addr,
                                                          uint16_t reg_addr, uint16_t quantity,
                                                          uint16_t *out, int32_t timeout_ms);

    /**
     * @brief Write a single holding register.
     *
     * @return Same classification as @ref read_holding_registers.
     */
    modbus_transaction_result_t (*write_single_register)(void *impl, uint8_t slave_addr,
                                                         uint16_t reg_addr, uint16_t value,
                                                         int32_t timeout_ms);

    /**
     * @brief Read a contiguous block of coils (discrete outputs) (FC01).
     *
     * @param[in]  impl       Implementation.
     * @param[in]  slave_addr Modbus unit identifier (1..247).
     * @param[in]  coil_addr  First coil address.
     * @param[in]  quantity   Number of coils (1..2000).
     * @param[out] out        Destination buffer, bit-packed LSB-first (one bit
     *                        per coil), at least ceil(quantity / 8) bytes.
     * @param[in]  timeout_ms Response timeout in milliseconds.
     *
     * @return Same classification as @ref read_holding_registers.
     */
    modbus_transaction_result_t (*read_coils)(void *impl, uint8_t slave_addr,
                                              uint16_t coil_addr, uint16_t quantity,
                                              uint8_t *out, int32_t timeout_ms);

    /**
     * @brief Write a single coil (FC05).
     *
     * @param[in] impl       Implementation.
     * @param[in] slave_addr Modbus unit identifier (1..247).
     * @param[in] coil_addr  Coil address.
     * @param[in] value      0xFF00 = ON, 0x0000 = OFF, 0x5500 = toggle.
     * @param[in] timeout_ms Response timeout in milliseconds.
     *
     * @return Same classification as @ref read_holding_registers.
     */
    modbus_transaction_result_t (*write_single_coil)(void *impl, uint8_t slave_addr,
                                                     uint16_t coil_addr, uint16_t value,
                                                     int32_t timeout_ms);

    /**
     * @brief Write multiple coils (FC0F).
     *
     * @param[in]  impl       Implementation.
     * @param[in]  slave_addr Modbus unit identifier (1..247).
     * @param[in]  coil_addr  First coil address.
     * @param[in]  quantity   Number of coils (1..2000).
     * @param[in]  values     Bit-packed values, LSB-first, one bit per coil.
     * @param[in]  timeout_ms Response timeout in milliseconds.
     *
     * @return Same classification as @ref read_holding_registers.
     */
    modbus_transaction_result_t (*write_multiple_coils)(void *impl, uint8_t slave_addr,
                                                        uint16_t coil_addr, uint16_t quantity,
                                                        const uint8_t *values, int32_t timeout_ms);

    /**
     * @brief Report the line configuration applied by the devicetree.
     *
     * There is no setter on purpose: the line parameters are compile-time
     * configuration and never change while the firmware runs.
     *
     * @return 0 on success, negative errno on failure.
     */
    int (*get_bus_cfg)(void *impl, modbus_bus_cfg_t *out);
} i_modbus_client_api_t;

/**
 * @brief Client handle: vtable plus opaque implementation pointer.
 *
 * Callers must use the `i_modbus_client_*()` inline wrappers below and never
 * dereference @ref api directly: the wrappers are the only place where the
 * handle and the arguments are validated before dispatch. Direct access is
 * reserved for the composition root while binding an implementation.
 */
typedef struct {
    const i_modbus_client_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;                       /**< Concrete client instance */
} i_modbus_client_t;

/**
 * @brief Check that a client handle is bound to an implementation.
 *
 * @param[in] client Handle to test (may be NULL).
 * @return true when both the vtable and the implementation are present.
 */
static inline bool i_modbus_client_is_bound(const i_modbus_client_t *client)
{
    return client != NULL && client->api != NULL && client->impl != NULL;
}

/*============================================================================*
 * VALIDATED OPERATIONS
 *
 * Every operation is a `static inline` wrapper. A rejected request returns
 * MODBUS_RESULT_INVALID for transactions (nothing was transmitted, the polling
 * loop treats it as a local rejection) and -EINVAL for configuration calls.
 * The checks here are structural only; wire-level errors stay the business of
 * the adapter.
 *============================================================================*/

/**
 * @brief Read a contiguous block of holding registers (FC03).
 *
 * @param[in]  client     Bound client handle.
 * @param[in]  slave_addr Modbus unit identifier (1..247; broadcast is not readable).
 * @param[in]  reg_addr   First register address.
 * @param[in]  quantity   Number of registers (1..MODBUS_READ_HOLDING_MAX_REGISTERS);
 *                        the block may not run past register 0xFFFF.
 * @param[out] out        Destination buffer of @p quantity words; must not be NULL.
 * @param[in]  timeout_ms Response timeout in milliseconds; must be positive.
 *
 * @return MODBUS_RESULT_OK on success, MODBUS_RESULT_EXCEPTION when the slave
 *         answered with a Modbus exception, MODBUS_RESULT_INVALID when the
 *         request was rejected before transmission, or the wire-level failure
 *         reported by the adapter.
 */
static inline modbus_transaction_result_t
i_modbus_client_read_holding_registers(const i_modbus_client_t *client, uint8_t slave_addr,
                                      uint16_t reg_addr, uint16_t quantity, uint16_t *out,
                                      int32_t timeout_ms)
{
    if (!i_modbus_client_is_bound(client) || out == NULL ||
        !modbus_slave_addr_is_valid(slave_addr) || quantity == 0U ||
        quantity > MODBUS_READ_HOLDING_MAX_REGISTERS || timeout_ms <= 0) {
        return MODBUS_RESULT_INVALID;
    }

    /* 0x10000 is one past the last addressable register. */
    if (((uint32_t) reg_addr + (uint32_t) quantity) > 0x10000U) {
        return MODBUS_RESULT_INVALID;
    }

    return client->api->read_holding_registers(client->impl, slave_addr, reg_addr, quantity, out,
                                               timeout_ms);
}

/**
 * @brief Write a single holding register (FC06).
 *
 * @param[in] client     Bound client handle.
 * @param[in] slave_addr Modbus unit identifier (1..247).
 * @param[in] reg_addr   Register address.
 * @param[in] value      Value to write.
 * @param[in] timeout_ms Response timeout in milliseconds; must be positive.
 *
 * @return Same classification as @ref i_modbus_client_read_holding_registers.
 */
static inline modbus_transaction_result_t
i_modbus_client_write_single_register(const i_modbus_client_t *client, uint8_t slave_addr,
                                     uint16_t reg_addr, uint16_t value, int32_t timeout_ms)
{
    if (!i_modbus_client_is_bound(client) || !modbus_slave_addr_is_valid(slave_addr) ||
        timeout_ms <= 0) {
        return MODBUS_RESULT_INVALID;
    }

    return client->api->write_single_register(client->impl, slave_addr, reg_addr, value,
                                              timeout_ms);
}

/**
 * @brief Read a contiguous block of coils (FC01).
 *
 * @param[in]  client     Bound client handle.
 * @param[in]  slave_addr Modbus unit identifier (1..247).
 * @param[in]  coil_addr  First coil address.
 * @param[in]  quantity   Number of coils (1..2000).
 * @param[out] out        Destination buffer, bit-packed LSB-first; must not be NULL.
 * @param[in]  timeout_ms Response timeout in milliseconds; must be positive.
 *
 * @return MODBUS_RESULT_OK on success, MODBUS_RESULT_EXCEPTION when the slave
 *         answered with a Modbus exception, MODBUS_RESULT_INVALID when the
 *         request was rejected before transmission, or the wire-level failure
 *         reported by the adapter.
 */
static inline modbus_transaction_result_t
i_modbus_client_read_coils(const i_modbus_client_t *client, uint8_t slave_addr,
                           uint16_t coil_addr, uint16_t quantity, uint8_t *out,
                           int32_t timeout_ms)
{
    if (!i_modbus_client_is_bound(client) || out == NULL ||
        !modbus_slave_addr_is_valid(slave_addr) || quantity == 0U ||
        quantity > 2000U || timeout_ms <= 0) {
        return MODBUS_RESULT_INVALID;
    }

    return client->api->read_coils(client->impl, slave_addr, coil_addr, quantity, out,
                                   timeout_ms);
}

/**
 * @brief Write a single coil (FC05).
 *
 * @param[in] client     Bound client handle.
 * @param[in] slave_addr Modbus unit identifier (1..247).
 * @param[in] coil_addr  Coil address.
 * @param[in] value      0xFF00 = ON, 0x0000 = OFF, 0x5500 = toggle.
 * @param[in] timeout_ms Response timeout in milliseconds; must be positive.
 *
 * @return Same classification as @ref i_modbus_client_read_coils.
 */
static inline modbus_transaction_result_t
i_modbus_client_write_single_coil(const i_modbus_client_t *client, uint8_t slave_addr,
                                  uint16_t coil_addr, uint16_t value, int32_t timeout_ms)
{
    if (!i_modbus_client_is_bound(client) || !modbus_slave_addr_is_valid(slave_addr) ||
        timeout_ms <= 0) {
        return MODBUS_RESULT_INVALID;
    }

    return client->api->write_single_coil(client->impl, slave_addr, coil_addr, value,
                                          timeout_ms);
}

/**
 * @brief Write multiple coils (FC0F).
 *
 * @param[in] client     Bound client handle.
 * @param[in] slave_addr Modbus unit identifier (1..247).
 * @param[in] coil_addr  First coil address.
 * @param[in] quantity   Number of coils (1..2000).
 * @param[in] values     Bit-packed values, LSB-first; must not be NULL.
 * @param[in] timeout_ms Response timeout in milliseconds; must be positive.
 *
 * @return Same classification as @ref i_modbus_client_read_coils.
 */
static inline modbus_transaction_result_t
i_modbus_client_write_multiple_coils(const i_modbus_client_t *client, uint8_t slave_addr,
                                     uint16_t coil_addr, uint16_t quantity,
                                     const uint8_t *values, int32_t timeout_ms)
{
    if (!i_modbus_client_is_bound(client) || values == NULL ||
        !modbus_slave_addr_is_valid(slave_addr) || quantity == 0U ||
        quantity > 2000U || timeout_ms <= 0) {
        return MODBUS_RESULT_INVALID;
    }

    return client->api->write_multiple_coils(client->impl, slave_addr, coil_addr, quantity,
                                             values, timeout_ms);
}

/**
 * @brief Report the line configuration currently applied.
 *
 * @param[in]  client Bound client handle.
 * @param[out] out    Destination; must not be NULL.
 *
 * @return 0 on success, -EINVAL on bad arguments, or the adapter error.
 */
static inline int i_modbus_client_get_bus_cfg(const i_modbus_client_t *client,
                                              modbus_bus_cfg_t *out)
{
    if (!i_modbus_client_is_bound(client) || out == NULL) {
        return -EINVAL;
    }

    return client->api->get_bus_cfg(client->impl, out);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_MODBUS_CLIENT_H_ */
