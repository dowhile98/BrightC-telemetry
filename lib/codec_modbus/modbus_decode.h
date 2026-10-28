/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modbus Register Unpacking & Codec Utilities.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_MODBUS_DECODE_H_
#define LIB_MODBUS_DECODE_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODBUS_ENDIANNESS_BE = 0,      /**< Big Endian: reg0 is MSW, reg1 is LSW (AB CD) */
    MODBUS_ENDIANNESS_LE,          /**< Little Endian: reg0 is LSW, reg1 is MSW (CD AB) */
    MODBUS_ENDIANNESS_BYTE_SWAP_BE,/**< Byte-swapped Big Endian (BA DC) */
    MODBUS_ENDIANNESS_BYTE_SWAP_LE /**< Byte-swapped Little Endian (DC BA) */
} modbus_word_order_t;

/**
 * @brief Decode two 16-bit Modbus registers into an IEEE-754 32-bit float.
 */
int modbus_decode_float32(uint16_t reg0, uint16_t reg1, modbus_word_order_t order, float *out_val);

/**
 * @brief Decode a 16-bit signed integer with linear scale factor.
 * E.g. reg = 245, scale = 0.1f -> out_val = 24.5f.
 */
int modbus_decode_int16_scaled(uint16_t reg, float scale_multiplier, float *out_val);

/**
 * @brief Decode a 16-bit unsigned integer with linear scale factor.
 */
int modbus_decode_uint16_scaled(uint16_t reg, float scale_multiplier, float *out_val);

/**
 * @brief Decode two 16-bit registers into a 32-bit signed integer.
 */
int modbus_decode_int32(uint16_t reg0, uint16_t reg1, bool word_swap, int32_t *out_val);

/**
 * @brief Decode two 16-bit registers into a 32-bit unsigned integer.
 */
int modbus_decode_uint32(uint16_t reg0, uint16_t reg1, bool word_swap, uint32_t *out_val);

#ifdef __cplusplus
}
#endif

#endif /* LIB_MODBUS_DECODE_H_ */
