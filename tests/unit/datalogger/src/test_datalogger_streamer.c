/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for Historical Datalogger Chunked Streamer Service.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "application/services/datalogger_streamer.h"
#include "application/active_objects/ao_datalogger.h"
#include "fake_storage_fs.h"
#include "fake_bkpsram.h"

static ao_datalogger_t s_streamer_ao;
static fake_bkpsram_t  s_mock_bkp;
static struct k_msgq   s_streamer_msgq;
static uint8_t         s_streamer_queue_buffer[16 * sizeof(datalogger_event_envelope_t)];
K_THREAD_STACK_DEFINE(s_streamer_datalogger_stack, 2048);

typedef struct {
    uint8_t buffer[8192];
    size_t  received_bytes;
    int     call_count;
    int     abort_on_call;
} test_sink_ctx_t;

static int test_sink_cb(const uint8_t *chunk, size_t len, void *user_data)
{
    test_sink_ctx_t *ctx = (test_sink_ctx_t *)user_data;
    ctx->call_count++;

    if (ctx->abort_on_call > 0 && ctx->call_count >= ctx->abort_on_call) {
        return -ECONNABORTED;
    }

    if (ctx->received_bytes + len <= sizeof(ctx->buffer)) {
        memcpy(ctx->buffer + ctx->received_bytes, chunk, len);
        ctx->received_bytes += len;
    }

    return 0;
}

static void *datalogger_streamer_suite_setup(void)
{
    return NULL;
}

static void datalogger_streamer_suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_storage_fs_reset();
    fake_bkpsram_init(&s_mock_bkp);
    fake_bkpsram_reset(&s_mock_bkp);

    k_msgq_init(&s_streamer_msgq, (char *)s_streamer_queue_buffer,
                sizeof(datalogger_event_envelope_t), 16);

    i_storage_fs_t fs = fake_storage_fs_get_port();
    (void)i_storage_fs_mount(&fs, "/SD:");
    i_bkpsram_t bkp = fake_bkpsram_port(&s_mock_bkp);
    zassert_equal(ao_datalogger_init(&s_streamer_ao, &fs, &bkp, &s_streamer_msgq,
                                     s_streamer_datalogger_stack,
                                     K_THREAD_STACK_SIZEOF(s_streamer_datalogger_stack), 8), 0);
}

static void datalogger_streamer_suite_after(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)ao_datalogger_stop(&s_streamer_ao);
    (void)ao_datalogger_wait_stopped(&s_streamer_ao, K_MSEC(500));
    (void)ao_datalogger_deinit(&s_streamer_ao);
}

ZTEST_SUITE(datalogger_streamer_suite, NULL, datalogger_streamer_suite_setup,
            datalogger_streamer_suite_before, datalogger_streamer_suite_after, NULL);

ZTEST(datalogger_streamer_suite, test_date_validation_and_sanitization)
{
    /* Valid dates */
    zassert_true(datalogger_streamer_is_valid_date("2026-11-12"));
    zassert_true(datalogger_streamer_is_valid_date("2024-02-29"));
    zassert_true(datalogger_streamer_is_valid_date("2099-12-31"));

    /* Traversal and invalid patterns */
    zassert_false(datalogger_streamer_is_valid_date(NULL));
    zassert_false(datalogger_streamer_is_valid_date(""));
    zassert_false(datalogger_streamer_is_valid_date("2026/11/12"));
    zassert_false(datalogger_streamer_is_valid_date("../../etc"));
    zassert_false(datalogger_streamer_is_valid_date("2026-11-12/evil"));
    zassert_false(datalogger_streamer_is_valid_date("2026-13-01"));
    zassert_false(datalogger_streamer_is_valid_date("2026-00-10"));
    zassert_false(datalogger_streamer_is_valid_date("2026-11-32"));
    zassert_false(datalogger_streamer_is_valid_date("1999-12-31"));
}

ZTEST(datalogger_streamer_suite, test_stream_existing_file_multi_chunk)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    datalogger_streamer_t streamer;
    zassert_equal(datalogger_streamer_init(&streamer, &fs, &s_streamer_ao, "/SD:"), 0);

    /* Populate fake file exceeding 2 KB chunk boundary (e.g. 4500 bytes) */
    const char *path = "/SD:/history/2026/11/2026-11-12_telemetry.csv";
    void *fh = NULL;
    zassert_equal(i_storage_fs_open(&fs, &fh, path, STORAGE_FS_OPEN_CREATE_ALWAYS | STORAGE_FS_OPEN_WRITE), 0);

    uint8_t sample_data[4500];
    for (size_t i = 0; i < sizeof(sample_data); i++) {
        sample_data[i] = (uint8_t)('A' + (i % 26));
    }
    size_t written = 0;
    zassert_equal(i_storage_fs_write(&fs, fh, sample_data, sizeof(sample_data), &written), 0);
    zassert_equal(written, sizeof(sample_data));
    zassert_equal(i_storage_fs_close(&fs, fh), 0);

    /* Stream file */
    test_sink_ctx_t sink_ctx;
    memset(&sink_ctx, 0, sizeof(sink_ctx));
    size_t total_sent = 0;

    int rc = datalogger_streamer_stream_file(&streamer, LOG_STREAM_TELEMETRY, "2026-11-12",
                                            test_sink_cb, &sink_ctx, &total_sent);
    zassert_equal(rc, 0);
    zassert_equal(total_sent, sizeof(sample_data));
    zassert_equal(sink_ctx.received_bytes, sizeof(sample_data));
    zassert_true(sink_ctx.call_count >= 5); /* 1024 * 4 + 404 bytes -> 5 chunks */
    zassert_equal(memcmp(sink_ctx.buffer, sample_data, sizeof(sample_data)), 0);
}

ZTEST(datalogger_streamer_suite, test_stream_missing_file_returns_noent)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    datalogger_streamer_t streamer;
    zassert_equal(datalogger_streamer_init(&streamer, &fs, &s_streamer_ao, "/SD:"), 0);

    test_sink_ctx_t sink_ctx;
    memset(&sink_ctx, 0, sizeof(sink_ctx));

    int rc = datalogger_streamer_stream_file(&streamer, LOG_STREAM_ALARMS, "2026-11-12",
                                            test_sink_cb, &sink_ctx, NULL);
    zassert_equal(rc, -ENOENT);
}

ZTEST(datalogger_streamer_suite, test_stream_sink_abort_propagation)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    datalogger_streamer_t streamer;
    zassert_equal(datalogger_streamer_init(&streamer, &fs, &s_streamer_ao, "/SD:"), 0);

    const char *path = "/SD:/logs/2026/11/2026-11-12_alarms.log";
    void *fh = NULL;
    zassert_equal(i_storage_fs_open(&fs, &fh, path, STORAGE_FS_OPEN_CREATE_ALWAYS | STORAGE_FS_OPEN_WRITE), 0);
    uint8_t dummy[4096];
    memset(dummy, 'X', sizeof(dummy));
    size_t written = 0;
    (void)i_storage_fs_write(&fs, fh, dummy, sizeof(dummy), &written);
    (void)i_storage_fs_close(&fs, fh);

    test_sink_ctx_t sink_ctx;
    memset(&sink_ctx, 0, sizeof(sink_ctx));
    sink_ctx.abort_on_call = 1; /* Abort immediately on first chunk */

    int rc = datalogger_streamer_stream_file(&streamer, LOG_STREAM_ALARMS, "2026-11-12",
                                            test_sink_cb, &sink_ctx, NULL);
    zassert_equal(rc, -ECONNABORTED);
}
