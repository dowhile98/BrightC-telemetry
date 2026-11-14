/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file bkpsram_ring.h
 * @brief Pure domain circular ring buffer for non-volatile Backup SRAM storage.
 *
 * ISO C99 / C11 compliant. Zero Zephyr headers, zero hardware registers.
 */

#ifndef APP_DOMAIN_STORAGE_BKPSRAM_RING_H_
#define APP_DOMAIN_STORAGE_BKPSRAM_RING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/datalogger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BKPSRAM_HEADER_MAGIC            0x424B5052U /* 'BKPR' */
#define BKPSRAM_DEFAULT_CAPACITY        63U
#define BKPSRAM_MIN_STORAGE_SIZE        4096U

/**
 * @brief Persistent ring buffer control header (64 bytes).
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< BKPSRAM_HEADER_MAGIC */
    uint32_t head;              /**< Index of next write slot [0..max_entries-1] */
    uint32_t count;             /**< Valid uncommitted / active entry count */
    uint32_t total_written;     /**< Monotonic counter of all records written */
    uint16_t header_crc16;      /**< CRC-16 across the 16 bytes prior */
    uint8_t  reserved[46];      /**< Pad to exact 64-byte block */
} bkpsram_header_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(bkpsram_header_t) == 64U, "bkpsram_header_t must be 64 bytes");
#endif

/**
 * @brief In-memory context handle for managing the ring buffer.
 */
typedef struct {
    uint8_t *raw_buf;
    size_t   buf_size;
    uint32_t max_entries;
    bool     is_initialized;
} bkpsram_ring_ctx_t;

/**
 * @brief Calculate CRC-16-CCITT checksum (poly: 0x1021, init: 0xFFFF).
 */
uint16_t bkpsram_calc_crc16(const uint8_t *data, size_t len);

/**
 * @brief Initialize ring buffer context over a designated memory buffer.
 *
 * Validates header integrity. If memory is uninitialized or corrupted,
 * automatically formats it clean.
 *
 * @param[out] ctx        Context to initialize.
 * @param[in]  storage    Base pointer to backing memory (must be at least 4096 bytes).
 * @param[in]  size_bytes Size of backing memory in bytes.
 * @return 0 on success, negative errno on invalid arguments.
 */
int bkpsram_ring_init(bkpsram_ring_ctx_t *ctx, void *storage, size_t size_bytes);

/**
 * @brief Unconditionally format/clear the ring buffer.
 *
 * @param[in,out] ctx Initialized ring buffer context.
 * @return 0 on success, negative errno on failure.
 */
int bkpsram_ring_format(bkpsram_ring_ctx_t *ctx);

/**
 * @brief Push a critical alarm record into the ring buffer.
 *
 * Computes CRC-16, writes packed record to slot, updates head pointer, and
 * updates header CRC. If buffer is full, oldest uncommitted slot is overwritten.
 *
 * @param[in,out] ctx    Initialized context.
 * @param[in]     record Alarm record to persist (CRC will be calculated automatically).
 * @return 0 on success, negative errno on failure.
 */
int bkpsram_ring_push(bkpsram_ring_ctx_t *ctx, alarm_nv_record_t *record);

/**
 * @brief Retrieve uncommitted alarm records sequentially for boot reconciliation.
 *
 * @param[in]  ctx         Initialized context.
 * @param[in]  cursor      Current search cursor (start at 0).
 * @param[out] out_record  Destination buffer for found uncommitted record.
 * @param[out] next_cursor Updated cursor to pass on subsequent call.
 * @return 0 if uncommitted record found, -ENOENT if no more uncommitted records, negative on error.
 */
int bkpsram_ring_get_next_uncommitted(const bkpsram_ring_ctx_t *ctx,
                                     uint32_t cursor,
                                     alarm_nv_record_t *out_record,
                                     uint32_t *next_cursor);

/**
 * @brief Mark an entry as committed to MicroSD by its unique sequence ID.
 *
 * @param[in,out] ctx    Initialized context.
 * @param[in]     seq_id Monotonic sequence identifier of the committed record.
 * @return 0 on success, -ENOENT if seq_id not found in buffer.
 */
int bkpsram_ring_mark_committed(const bkpsram_ring_ctx_t *ctx, uint32_t seq_id);

/**
 * @brief Query number of uncommitted entries currently in the ring buffer.
 */
uint32_t bkpsram_ring_uncommitted_count(const bkpsram_ring_ctx_t *ctx);

/**
 * @brief Check if the ring buffer header is valid.
 */
bool bkpsram_ring_is_valid(const bkpsram_ring_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_STORAGE_BKPSRAM_RING_H_ */
