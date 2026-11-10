/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file config_envelope.h
 * @brief Common integrity and versioning envelope for non-volatile configuration domains.
 *
 * Implements a standardized envelope header containing:
 * - Magic word (0x4243544C "BCTL")
 * - Schema version
 * - Logical domain tag
 * - Payload length
 * - CRC-16-CCITT payload checksum
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_COMMON_CONFIG_ENVELOPE_H_
#define APP_COMMON_CONFIG_ENVELOPE_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_ENVELOPE_MAGIC           0x4243544CU  /**< ASCII 'BCTL' (BrightC Telemetry) */
#define CONFIG_ENVELOPE_VERSION_V1      1U
#define CONFIG_ENVELOPE_VERSION_V2      2U
#define CONFIG_ENVELOPE_VERSION_CURRENT CONFIG_ENVELOPE_VERSION_V2

/**
 * @brief Common binary envelope header preceding persisted configuration payloads.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /**< Magic identifier: CONFIG_ENVELOPE_MAGIC (0x4243544C) */
    uint16_t version;      /**< Envelope/schema version */
    uint16_t domain;       /**< config_domain_t logical category */
    uint16_t payload_len;  /**< Size of payload in bytes following this header */
    uint16_t crc16;        /**< CRC-16-CCITT computed across payload bytes */
} config_envelope_header_t;

#define CONFIG_ENVELOPE_HEADER_SIZE sizeof(config_envelope_header_t)

/**
 * @brief Pure C CRC-16-CCITT computation (poly: 0x1021, init: 0xFFFF).
 *
 * @param data Pointer to buffer.
 * @param len  Length of buffer in bytes.
 * @return 16-bit CRC checksum.
 */
static inline uint16_t config_envelope_crc16(const uint8_t *data, size_t len)
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

/**
 * @brief Pack domain configuration payload into a verified binary envelope.
 *
 * @param[in]  domain        Logical domain category.
 * @param[in]  payload       Pointer to raw domain configuration data.
 * @param[in]  payload_len   Size of raw payload in bytes.
 * @param[out] out_buf       Destination buffer receiving header + payload.
 * @param[in]  max_out_size  Capacity of destination buffer.
 * @param[out] out_size      Actual total bytes written (header + payload).
 * @return 0 on success, -EINVAL on invalid arguments, -ENOSPC if out_buf too small.
 */
static inline int config_envelope_pack(config_domain_t domain,
                                       const void *payload,
                                       size_t payload_len,
                                       void *out_buf,
                                       size_t max_out_size,
                                       size_t *out_size)
{
    if (!config_domain_is_valid(domain) || payload == NULL || out_buf == NULL ||
        payload_len == 0U || payload_len > 0xFFFFU) {
        return -EINVAL;
    }

    size_t total_needed = CONFIG_ENVELOPE_HEADER_SIZE + payload_len;
    if (max_out_size < total_needed) {
        return -ENOSPC;
    }

    config_envelope_header_t hdr;
    hdr.magic = CONFIG_ENVELOPE_MAGIC;
    hdr.version = CONFIG_ENVELOPE_VERSION_CURRENT;
    hdr.domain = (uint16_t)domain;
    hdr.payload_len = (uint16_t)payload_len;
    hdr.crc16 = config_envelope_crc16((const uint8_t *)payload, payload_len);

    uint8_t *dst = (uint8_t *)out_buf;
    (void)memcpy(dst, &hdr, CONFIG_ENVELOPE_HEADER_SIZE);
    (void)memcpy(dst + CONFIG_ENVELOPE_HEADER_SIZE, payload, payload_len);

    if (out_size != NULL) {
        *out_size = total_needed;
    }

    return 0;
}

/**
 * @brief Unpack, validate integrity, and extract payload from a configuration envelope.
 *
 * Verifies magic number, version compatibility, domain match, and CRC16 checksum.
 *
 * @param[in]  expected_domain Expected logical domain category.
 * @param[in]  in_buf          Buffer containing received envelope.
 * @param[in]  in_size         Total bytes in envelope buffer.
 * @param[out] out_payload     Destination buffer receiving raw configuration.
 * @param[in]  max_payload_len Capacity of destination buffer.
 * @param[out] out_payload_len Actual size of unpacked payload in bytes.
 * @return 0 on success,
 *         -EINVAL on null or corrupted arguments,
 *         -EBADMSG on invalid magic, unsupported version, domain mismatch, or CRC mismatch,
 *         -ENOBUFS if output buffer cannot hold payload.
 */
static inline int config_envelope_unpack(config_domain_t expected_domain,
                                         const void *in_buf,
                                         size_t in_size,
                                         void *out_payload,
                                         size_t max_payload_len,
                                         size_t *out_payload_len)
{
    if (in_buf == NULL || !config_domain_is_valid(expected_domain) || out_payload == NULL) {
        return -EINVAL;
    }

    if (in_size < CONFIG_ENVELOPE_HEADER_SIZE) {
        return -EBADMSG;
    }

    config_envelope_header_t hdr;
    (void)memcpy(&hdr, in_buf, CONFIG_ENVELOPE_HEADER_SIZE);

    if (hdr.magic != CONFIG_ENVELOPE_MAGIC) {
        return -EBADMSG;
    }

    if (hdr.version != CONFIG_ENVELOPE_VERSION_CURRENT) {
        return -EBADMSG;
    }

    if ((config_domain_t)hdr.domain != expected_domain) {
        return -EBADMSG;
    }

    if (in_size < CONFIG_ENVELOPE_HEADER_SIZE + (size_t)hdr.payload_len) {
        return -EBADMSG;
    }

    if (max_payload_len < (size_t)hdr.payload_len) {
        return -ENOBUFS;
    }

    const uint8_t *payload_ptr = ((const uint8_t *)in_buf) + CONFIG_ENVELOPE_HEADER_SIZE;
    uint16_t calculated_crc = config_envelope_crc16(payload_ptr, hdr.payload_len);

    if (calculated_crc != hdr.crc16) {
        return -EBADMSG;
    }

    (void)memcpy(out_payload, payload_ptr, hdr.payload_len);

    if (out_payload_len != NULL) {
        *out_payload_len = (size_t)hdr.payload_len;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_CONFIG_ENVELOPE_H_ */
