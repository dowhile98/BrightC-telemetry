/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file crc16.h
 * @brief Pure C CRC-16-CCITT utility (poly: 0x1021, init: 0xFFFF).
 *
 * Standalone, pure C99/C11 checksum computation with zero external dependencies.
 */

#ifndef APP_COMMON_CRC16_H_
#define APP_COMMON_CRC16_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure C CRC-16-CCITT computation (poly: 0x1021, init: 0xFFFF).
 *
 * @param data Pointer to buffer.
 * @param len  Length of buffer in bytes.
 * @return 16-bit CRC checksum.
 */
static inline uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    if (data == NULL) {
        return 0U;
    }

    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_CRC16_H_ */
