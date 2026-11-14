/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for domain Backup SRAM ring buffer.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "domain/storage/bkpsram_ring.h"

static uint8_t s_mock_storage[BKPSRAM_MIN_STORAGE_SIZE];
static bkpsram_ring_ctx_t s_ctx;

static void *bkpsram_setup(void)
{
    memset(s_mock_storage, 0, sizeof(s_mock_storage));
    datalogger_set_seq_id(1U);
    return NULL;
}

static void bkpsram_before(void *fixture)
{
    (void)fixture;
    datalogger_set_seq_id(1U);
    memset(s_mock_storage, 0, sizeof(s_mock_storage));
    zassert_equal(bkpsram_ring_init(&s_ctx, s_mock_storage, sizeof(s_mock_storage)), 0);
}

ZTEST_SUITE(bkpsram_ring_suite, NULL, bkpsram_setup, bkpsram_before, NULL, NULL);

ZTEST(bkpsram_ring_suite, test_init_clean_and_invalid_args)
{
    bkpsram_ring_ctx_t ctx;

    /* Invalid inputs */
    zassert_equal(bkpsram_ring_init(NULL, s_mock_storage, sizeof(s_mock_storage)), -EINVAL);
    zassert_equal(bkpsram_ring_init(&ctx, NULL, sizeof(s_mock_storage)), -EINVAL);
    zassert_equal(bkpsram_ring_init(&ctx, s_mock_storage, 1024), -EINVAL);

    /* Clean init */
    zassert_equal(bkpsram_ring_init(&ctx, s_mock_storage, sizeof(s_mock_storage)), 0);
    zassert_true(bkpsram_ring_is_valid(&ctx));
    zassert_equal(bkpsram_ring_uncommitted_count(&ctx), 0);
}

ZTEST(bkpsram_ring_suite, test_push_and_query_single_record)
{
    alarm_nv_record_t rec = {
        .seq_id = datalogger_next_seq_id(),
        .uptime_ms = 1250,
        .epoch_s = 1731550000,
        .alarm_id = ALARM_ID_AC1_HIGH_PRESSURE,
        .source_subsystem = ALARM_SOURCE_HVAC_CONTROL,
        .transition_type = ALARM_EVENT_ACTIVATED,
        .threshold_value = 28.5f,
        .measured_value = 31.2f,
        .severity = ALARM_SEVERITY_CRITICAL,
        .status_flags = ALARM_NV_FLAG_NONE,
        .tag = "AC1_HIGH_PRESS_TRIP"
    };

    zassert_equal(bkpsram_ring_push(&s_ctx, &rec), 0);
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 1);

    alarm_nv_record_t recovered;
    uint32_t next_cursor = 0;
    int ret = bkpsram_ring_get_next_uncommitted(&s_ctx, 0, &recovered, &next_cursor);
    zassert_equal(ret, 0);
    zassert_equal(recovered.seq_id, 1);
    zassert_equal(recovered.alarm_id, ALARM_ID_AC1_HIGH_PRESSURE);
    zassert_equal(recovered.severity, ALARM_SEVERITY_CRITICAL);
    zassert_equal(strcmp(recovered.tag, "AC1_HIGH_PRESS_TRIP"), 0);
    zassert_equal(recovered.status_flags & ALARM_NV_FLAG_COMMITTED_SD, 0);

    /* Mark as committed */
    zassert_equal(bkpsram_ring_mark_committed(&s_ctx, 1), 0);
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 0);

    /* Second query should return -ENOENT */
    ret = bkpsram_ring_get_next_uncommitted(&s_ctx, 0, &recovered, &next_cursor);
    zassert_equal(ret, -ENOENT);
}

ZTEST(bkpsram_ring_suite, test_push_wraparound_overwrites_oldest)
{
    /* Buffer capacity is 63 entries */
    for (uint32_t i = 1; i <= 70; i++) {
        alarm_nv_record_t rec = {
            .seq_id = datalogger_next_seq_id(),
            .uptime_ms = i * 1000,
            .alarm_id = (uint16_t)i,
            .severity = ALARM_SEVERITY_CRITICAL,
            .status_flags = ALARM_NV_FLAG_NONE,
            .tag = "ALARM_RECORD"
        };
        zassert_equal(bkpsram_ring_push(&s_ctx, &rec), 0);
    }

    /* Uncommitted count should cap at capacity (63) */
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 63);

    /* Oldest 7 entries (1..7) were overwritten, entries 8..70 remain */
    alarm_nv_record_t rec;
    uint32_t cursor = 0;
    uint32_t found_count = 0;
    uint32_t min_seq = 0xFFFFFFFF;
    uint32_t max_seq = 0;

    while (bkpsram_ring_get_next_uncommitted(&s_ctx, cursor, &rec, &cursor) == 0) {
        found_count++;
        if (rec.seq_id < min_seq) {
            min_seq = rec.seq_id;
        }
        if (rec.seq_id > max_seq) {
            max_seq = rec.seq_id;
        }
    }

    zassert_equal(found_count, 63);
    zassert_equal(min_seq, 8);
    zassert_equal(max_seq, 70);
}

ZTEST(bkpsram_ring_suite, test_crc_corruption_is_skipped)
{
    alarm_nv_record_t rec = {
        .seq_id = 999,
        .uptime_ms = 5000,
        .alarm_id = 12,
        .severity = ALARM_SEVERITY_FATAL,
        .status_flags = ALARM_NV_FLAG_NONE,
        .tag = "CORRUPT_TEST"
    };

    zassert_equal(bkpsram_ring_push(&s_ctx, &rec), 0);
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 1);

    /* Intentionally corrupt a byte inside the record slot (e.g. at offset 70) */
    s_mock_storage[70] ^= 0xFF;

    /* Entry should now fail CRC verification and be ignored */
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 0);

    alarm_nv_record_t out_rec;
    uint32_t cursor = 0;
    zassert_equal(bkpsram_ring_get_next_uncommitted(&s_ctx, 0, &out_rec, &cursor), -ENOENT);
}

ZTEST(bkpsram_ring_suite, test_reboot_retention_and_seq_id_recovery)
{
    datalogger_set_seq_id(100);

    alarm_nv_record_t rec1 = {
        .seq_id = datalogger_next_seq_id(),
        .alarm_id = 10,
        .severity = ALARM_SEVERITY_CRITICAL,
        .tag = "REC_100"
    };
    alarm_nv_record_t rec2 = {
        .seq_id = datalogger_next_seq_id(),
        .alarm_id = 11,
        .severity = ALARM_SEVERITY_CRITICAL,
        .tag = "REC_101"
    };

    zassert_equal(bkpsram_ring_push(&s_ctx, &rec1), 0);
    zassert_equal(bkpsram_ring_push(&s_ctx, &rec2), 0);

    /* Simulate software reset / reboot: clear in-memory context and reset seq_id to 1 */
    memset(&s_ctx, 0, sizeof(s_ctx));
    datalogger_set_seq_id(1);

    /* Re-initialize over the preserved mock storage */
    zassert_equal(bkpsram_ring_init(&s_ctx, s_mock_storage, sizeof(s_mock_storage)), 0);

    /* Uncommitted count should still be 2 */
    zassert_equal(bkpsram_ring_uncommitted_count(&s_ctx), 2);

    /* Global sequence counter must have resumed from > highest record seq_id (101 -> 102) */
    zassert_true(datalogger_next_seq_id() >= 102);
}

#include "fake_bkpsram.h"

ZTEST(bkpsram_ring_suite, test_port_interface_and_fake_adapter)
{
    fake_bkpsram_t fake;
    zassert_equal(fake_bkpsram_init(&fake), 0);

    i_bkpsram_t port = fake_bkpsram_port(&fake);
    zassert_true(i_bkpsram_is_bound(&port));
    zassert_true(i_bkpsram_is_valid(&port));
    zassert_equal(i_bkpsram_uncommitted_count(&port), 0);

    alarm_nv_record_t rec = {
        .seq_id = 42,
        .alarm_id = 99,
        .severity = ALARM_SEVERITY_FATAL,
        .tag = "PORT_TEST"
    };

    zassert_equal(i_bkpsram_write_record(&port, &rec), 0);
    zassert_equal(i_bkpsram_uncommitted_count(&port), 1);

    alarm_nv_record_t out_rec;
    uint32_t next_cur = 0;
    zassert_equal(i_bkpsram_get_next_uncommitted(&port, 0, &out_rec, &next_cur), 0);
    zassert_equal(out_rec.seq_id, 42);

    zassert_equal(i_bkpsram_mark_committed(&port, 42), 0);
    zassert_equal(i_bkpsram_uncommitted_count(&port), 0);

    /* Test unbound port handling */
    i_bkpsram_t null_port = {0};
    zassert_false(i_bkpsram_is_bound(&null_port));
    zassert_equal(i_bkpsram_write_record(&null_port, &rec), -EINVAL);
}

