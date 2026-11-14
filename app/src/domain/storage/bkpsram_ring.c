/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file bkpsram_ring.c
 * @brief Implementation of pure domain ring buffer for non-volatile Backup SRAM.
 */

#include "domain/storage/bkpsram_ring.h"

#include <errno.h>
#include <string.h>

/* Global atomic sequence ID counter for post-boot traceability */
static uint32_t s_datalogger_global_seq_id = 1U;

uint32_t datalogger_next_seq_id(void)
{
    return s_datalogger_global_seq_id++;
}

void datalogger_set_seq_id(uint32_t val)
{
    s_datalogger_global_seq_id = val;
}

uint16_t bkpsram_calc_crc16(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U) {
        return 0U;
    }

    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

static inline bkpsram_header_t *get_header(const bkpsram_ring_ctx_t *ctx)
{
    return (bkpsram_header_t *)ctx->raw_buf;
}

static inline alarm_nv_record_t *get_entry(const bkpsram_ring_ctx_t *ctx, uint32_t index)
{
    size_t offset = sizeof(bkpsram_header_t) + ((size_t)index * sizeof(alarm_nv_record_t));
    return (alarm_nv_record_t *)(ctx->raw_buf + offset);
}

static uint16_t calc_header_crc(const bkpsram_header_t *hdr)
{
    /* Calculate CRC across magic, head, count, and total_written (16 bytes) */
    return bkpsram_calc_crc16((const uint8_t *)hdr, 16U);
}

static void update_header_crc(bkpsram_header_t *hdr)
{
    hdr->header_crc16 = calc_header_crc(hdr);
}

static bool is_header_valid(const bkpsram_header_t *hdr, uint32_t max_entries)
{
    if (hdr->magic != BKPSRAM_HEADER_MAGIC) {
        return false;
    }
    if (hdr->head >= max_entries || hdr->count > max_entries) {
        return false;
    }
    if (hdr->header_crc16 != calc_header_crc(hdr)) {
        return false;
    }
    return true;
}

int bkpsram_ring_init(bkpsram_ring_ctx_t *ctx, void *storage, size_t size_bytes)
{
    if (ctx == NULL || storage == NULL) {
        return -EINVAL;
    }

    if (size_bytes < BKPSRAM_MIN_STORAGE_SIZE) {
        return -EINVAL;
    }

    ctx->raw_buf = (uint8_t *)storage;
    ctx->buf_size = size_bytes;
    ctx->max_entries = (uint32_t)((size_bytes - sizeof(bkpsram_header_t)) / sizeof(alarm_nv_record_t));
    if (ctx->max_entries > BKPSRAM_DEFAULT_CAPACITY) {
        ctx->max_entries = BKPSRAM_DEFAULT_CAPACITY;
    }

    const bkpsram_header_t *hdr = get_header(ctx);
    if (!is_header_valid(hdr, ctx->max_entries)) {
        /* Storage is uninitialized, power-cut, or corrupted: format clean */
        int ret = bkpsram_ring_format(ctx);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* Update global sequence counter from highest committed/written seq_id if found */
        uint32_t max_seq = 0;
        for (uint32_t i = 0; i < ctx->max_entries; i++) {
            alarm_nv_record_t *rec = get_entry(ctx, i);
            if (rec->magic == ALARM_NV_RECORD_MAGIC) {
                if (rec->crc16 == bkpsram_calc_crc16((const uint8_t *)rec, sizeof(alarm_nv_record_t) - 2U)) {
                    if (rec->seq_id > max_seq) {
                        max_seq = rec->seq_id;
                    }
                }
            }
        }
        if (max_seq > 0U && max_seq >= s_datalogger_global_seq_id) {
            s_datalogger_global_seq_id = max_seq + 1U;
        }
    }

    ctx->is_initialized = true;
    return 0;
}

int bkpsram_ring_format(bkpsram_ring_ctx_t *ctx)
{
    if (ctx == NULL || ctx->raw_buf == NULL) {
        return -EINVAL;
    }

    memset(ctx->raw_buf, 0, ctx->buf_size);

    bkpsram_header_t *hdr = get_header(ctx);
    hdr->magic = BKPSRAM_HEADER_MAGIC;
    hdr->head = 0U;
    hdr->count = 0U;
    hdr->total_written = 0U;
    update_header_crc(hdr);

    return 0;
}

int bkpsram_ring_push(bkpsram_ring_ctx_t *ctx, alarm_nv_record_t *record)
{
    if (ctx == NULL || !ctx->is_initialized || record == NULL) {
        return -EINVAL;
    }

    bkpsram_header_t *hdr = get_header(ctx);
    if (!is_header_valid(hdr, ctx->max_entries)) {
        (void)bkpsram_ring_format(ctx);
        hdr = get_header(ctx);
    }

    /* Assign magic and calculate CRC-16 across bytes [0 .. sizeof(record)-3] */
    record->magic = ALARM_NV_RECORD_MAGIC;
    record->crc16 = bkpsram_calc_crc16((const uint8_t *)record, sizeof(alarm_nv_record_t) - 2U);

    uint32_t write_idx = hdr->head;
    alarm_nv_record_t *slot = get_entry(ctx, write_idx);
    memcpy(slot, record, sizeof(alarm_nv_record_t));

    hdr->head = (write_idx + 1U) % ctx->max_entries;
    if (hdr->count < ctx->max_entries) {
        hdr->count++;
    }
    hdr->total_written++;
    update_header_crc(hdr);

    return 0;
}

int bkpsram_ring_get_next_uncommitted(const bkpsram_ring_ctx_t *ctx,
                                     uint32_t cursor,
                                     alarm_nv_record_t *out_record,
                                     uint32_t *next_cursor)
{
    if (ctx == NULL || !ctx->is_initialized || out_record == NULL || next_cursor == NULL) {
        return -EINVAL;
    }

    const bkpsram_header_t *hdr = get_header(ctx);
    if (!is_header_valid(hdr, ctx->max_entries)) {
        return -EIO;
    }

    for (uint32_t i = cursor; i < ctx->max_entries; i++) {
        alarm_nv_record_t *entry = get_entry(ctx, i);
        if (entry->magic == ALARM_NV_RECORD_MAGIC) {
            uint16_t expected_crc = bkpsram_calc_crc16((const uint8_t *)entry, sizeof(alarm_nv_record_t) - 2U);
            if (entry->crc16 == expected_crc) {
                if ((entry->status_flags & ALARM_NV_FLAG_COMMITTED_SD) == 0U) {
                    memcpy(out_record, entry, sizeof(alarm_nv_record_t));
                    *next_cursor = i + 1U;
                    return 0;
                }
            }
        }
    }

    *next_cursor = ctx->max_entries;
    return -ENOENT;
}

int bkpsram_ring_mark_committed(const bkpsram_ring_ctx_t *ctx, uint32_t seq_id)
{
    if (ctx == NULL || !ctx->is_initialized) {
        return -EINVAL;
    }

    bkpsram_header_t *hdr = get_header(ctx);
    if (!is_header_valid(hdr, ctx->max_entries)) {
        return -EIO;
    }

    for (uint32_t i = 0; i < ctx->max_entries; i++) {
        alarm_nv_record_t *entry = get_entry(ctx, i);
        if (entry->magic == ALARM_NV_RECORD_MAGIC && entry->seq_id == seq_id) {
            uint16_t expected_crc = bkpsram_calc_crc16((const uint8_t *)entry, sizeof(alarm_nv_record_t) - 2U);
            if (entry->crc16 == expected_crc) {
                entry->status_flags |= ALARM_NV_FLAG_COMMITTED_SD;
                entry->crc16 = bkpsram_calc_crc16((const uint8_t *)entry, sizeof(alarm_nv_record_t) - 2U);
                if (hdr->count > 0U) {
                    hdr->count--;
                    update_header_crc(hdr);
                }
                return 0;
            }
        }
    }

    return -ENOENT;
}

uint32_t bkpsram_ring_uncommitted_count(const bkpsram_ring_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->is_initialized) {
        return 0U;
    }

    const bkpsram_header_t *hdr = get_header(ctx);
    if (!is_header_valid(hdr, ctx->max_entries)) {
        return 0U;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < ctx->max_entries; i++) {
        alarm_nv_record_t *entry = get_entry(ctx, i);
        if (entry->magic == ALARM_NV_RECORD_MAGIC) {
            uint16_t expected_crc = bkpsram_calc_crc16((const uint8_t *)entry, sizeof(alarm_nv_record_t) - 2U);
            if (entry->crc16 == expected_crc && (entry->status_flags & ALARM_NV_FLAG_COMMITTED_SD) == 0U) {
                count++;
            }
        }
    }
    return count;
}

bool bkpsram_ring_is_valid(const bkpsram_ring_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->is_initialized) {
        return false;
    }
    return is_header_valid(get_header(ctx), ctx->max_entries);
}
