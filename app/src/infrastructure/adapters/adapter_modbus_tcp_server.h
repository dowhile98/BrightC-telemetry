/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_modbus_tcp_server.h
 * @brief Infrastructure Adapter for nanoMODBUS TCP Server callbacks and memory mapping.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_TCP_SERVER_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_TCP_SERVER_H_

#include <stdbool.h>
#include <stdint.h>

#include <nanomodbus.h>

#include "common/modbus_tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Concrete adapter structure for Modbus TCP Server.
 */
typedef struct {
    nmbs_callbacks      callbacks;               /**< nanoMODBUS callback table */
    modbus_word_order_t word_order;              /**< Endianness for 32-bit registers */
    bool                is_initialized;          /**< True if adapter is configured */

    /* Supervisory command state tracking */
    uint16_t            supervisory_opcode;      /**< Current/requested opcode */
    uint16_t            supervisory_arg;         /**< Current/requested argument */
    uint16_t            supervisory_status;      /**< 0=Ready, 1=Success, 2=Error */
    uint16_t            supervisory_last_opcode; /**< Last executed opcode */
    uint16_t            supervisory_last_errno;  /**< Last error code (0 on success) */
} adapter_modbus_tcp_server_t;

/**
 * @brief Initialize adapter with server configuration.
 *
 * @param[out] self   Adapter instance to initialize.
 * @param[in]  config Configuration parameters.
 * @return 0 on success, negative errno on error.
 */
int adapter_modbus_tcp_server_init(adapter_modbus_tcp_server_t *self,
                                   const modbus_tcp_server_config_t *config);

/**
 * @brief Retrieve initialized nanoMODBUS callback table.
 *
 * @param[in] self Adapter instance.
 * @return Pointer to callback table, or NULL if uninitialized.
 */
const nmbs_callbacks *adapter_modbus_tcp_server_get_callbacks(const adapter_modbus_tcp_server_t *self);

/* ========================================================================= */
/* nanoMODBUS Server Callback Handlers                                       */
/* ========================================================================= */

nmbs_error adapter_modbus_tcp_on_read_coils(uint16_t address, uint16_t quantity,
                                            nmbs_bitfield coils_out, uint8_t unit_id,
                                            void *arg);

nmbs_error adapter_modbus_tcp_on_write_single_coil(uint16_t address, bool value,
                                                   uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_write_multiple_coils(uint16_t address, uint16_t quantity,
                                                      const nmbs_bitfield coils,
                                                      uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_read_discrete_inputs(uint16_t address, uint16_t quantity,
                                                      nmbs_bitfield inputs_out,
                                                      uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_read_input_registers(uint16_t address, uint16_t quantity,
                                                      uint16_t *registers_out,
                                                      uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_read_holding_registers(uint16_t address, uint16_t quantity,
                                                        uint16_t *registers_out,
                                                        uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_write_single_register(uint16_t address, uint16_t value,
                                                       uint8_t unit_id, void *arg);

nmbs_error adapter_modbus_tcp_on_write_multiple_registers(uint16_t address, uint16_t quantity,
                                                          const uint16_t *registers,
                                                          uint8_t unit_id, void *arg);

/* ========================================================================= */
/* Pure Register Packing & Helper Utilities                                  */
/* ========================================================================= */

/**
 * @brief Pack 32-bit unsigned integer into two 16-bit Modbus registers.
 */
static inline void modbus_tcp_pack_32(uint16_t *dest, uint32_t val, modbus_word_order_t order)
{
    if (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) {
        dest[0] = (uint16_t)(val & 0xFFFFU);
        dest[1] = (uint16_t)((val >> 16) & 0xFFFFU);
    } else {
        dest[0] = (uint16_t)((val >> 16) & 0xFFFFU);
        dest[1] = (uint16_t)(val & 0xFFFFU);
    }
}

/**
 * @brief Pack 32-bit IEEE-754 float into two 16-bit Modbus registers.
 */
static inline void modbus_tcp_pack_float(uint16_t *dest, float val, modbus_word_order_t order)
{
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = val;
    modbus_tcp_pack_32(dest, conv.u, order);
}

/**
 * @brief Unpack two 16-bit Modbus registers into a 32-bit unsigned integer.
 */
static inline uint32_t modbus_tcp_unpack_32(const uint16_t *src, modbus_word_order_t order)
{
    if (order == MODBUS_WORD_ORDER_LITTLE_ENDIAN) {
        return ((uint32_t)src[1] << 16) | (uint32_t)src[0];
    } else {
        return ((uint32_t)src[0] << 16) | (uint32_t)src[1];
    }
}

/**
 * @brief Unpack two 16-bit Modbus registers into a 32-bit IEEE-754 float.
 */
static inline float modbus_tcp_unpack_float(const uint16_t *src, modbus_word_order_t order)
{
    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = modbus_tcp_unpack_32(src, order);
    return conv.f;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_TCP_SERVER_H_ */
