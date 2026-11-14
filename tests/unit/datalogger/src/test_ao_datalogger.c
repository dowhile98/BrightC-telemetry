/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for Unified Datalogger Active Object (ao_datalogger).
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "application/active_objects/ao_datalogger.h"
#include "fake_storage_fs.h"
#include "fake_bkpsram.h"

K_THREAD_STACK_DEFINE(s_test_datalogger_stack, 4096);
K_MSGQ_DEFINE(s_test_datalogger_queue, sizeof(datalogger_event_envelope_t), 32, 4);

static ao_datalogger_t s_datalogger;
static fake_bkpsram_t  s_fake_bkpsram;

static void *datalogger_test_setup(void)
{
    fake_storage_fs_reset();
    fake_bkpsram_init(&s_fake_bkpsram);
    return NULL;
}

static void datalogger_test_before(void *fixture)
{
    (void)fixture;
    k_msgq_purge(&s_test_datalogger_queue);
    fake_storage_fs_reset();
    fake_bkpsram_init(&s_fake_bkpsram);

    i_storage_fs_t fs = fake_storage_fs_get_port();
    (void)i_storage_fs_mount(&fs, "/SD:");

    i_bkpsram_t bkp = fake_bkpsram_port(&s_fake_bkpsram);

    zassert_equal(ao_datalogger_init(&s_datalogger,
                                    &fs,
                                    &bkp,
                                    &s_test_datalogger_queue,
                                    s_test_datalogger_stack,
                                    K_THREAD_STACK_SIZEOF(s_test_datalogger_stack),
                                    AO_DATALOGGER_PRIORITY),
                  0);
}

static void datalogger_test_after(void *fixture)
{
    (void)fixture;
    (void)ao_datalogger_deinit(&s_datalogger);
    k_msgq_purge(&s_test_datalogger_queue);
}

ZTEST_SUITE(ao_datalogger_suite, NULL, datalogger_test_setup, datalogger_test_before, datalogger_test_after, NULL);

ZTEST(ao_datalogger_suite, test_lifecycle_start_and_stop)
{
    zassert_true(s_datalogger.is_initialized);
    zassert_false(s_datalogger.is_running);

    zassert_equal(ao_datalogger_start(&s_datalogger), 0);
    zassert_true(s_datalogger.is_running);

    /* Double start should fail */
    zassert_equal(ao_datalogger_start(&s_datalogger), -EINVAL);

    /* Graceful stop */
    zassert_equal(ao_datalogger_stop(&s_datalogger), 0);
    zassert_equal(ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000)), 0);
    zassert_false(s_datalogger.is_running);
}

ZTEST(ao_datalogger_suite, test_storage_mount_status_and_reconciliation)
{
    /* Prime fake_bkpsram with an uncommitted critical alarm record */
    alarm_nv_record_t uncommitted = {
        .seq_id = 101,
        .uptime_ms = 4500,
        .alarm_id = ALARM_ID_AC1_HIGH_PRESSURE,
        .severity = ALARM_SEVERITY_CRITICAL,
        .status_flags = ALARM_NV_FLAG_NONE,
        .tag = "AC1_HP_FAULT"
    };
    i_bkpsram_t bkp = fake_bkpsram_port(&s_fake_bkpsram);
    zassert_equal(i_bkpsram_write_record(&bkp, &uncommitted), 0);
    zassert_equal(i_bkpsram_uncommitted_count(&bkp), 1);

    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    /* Post storage mounted event */
    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &mount_evt), 0);

    /* Wait briefly for active object processing */
    k_sleep(K_MSEC(50));

    /* Flush streams */
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    /* BKPSRAM record should be marked committed */
    zassert_equal(i_bkpsram_uncommitted_count(&bkp), 0);

    ao_datalogger_stats_t stats;
    zassert_equal(ao_datalogger_get_stats(&s_datalogger, &stats), 0);
    zassert_true(stats.bkpsram_recovered >= 1);

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}

ZTEST(ao_datalogger_suite, test_alarm_event_logging)
{
    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    /* Mark storage as mounted */
    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    (void)ao_datalogger_post_event(&s_datalogger, &mount_evt);
    k_sleep(K_MSEC(20));

    /* Post alarm transition */
    datalogger_event_envelope_t alarm_evt = {
        .type = DATALOGGER_EVT_ALARM_TRANSITION,
        .seq_id = 55,
        .uptime_ms = 12000,
        .payload.alarm = {
            .alarm_id = ALARM_ID_FACILITY_FIRE,
            .event_type = ALARM_EVENT_ACTIVATED,
            .severity = ALARM_SEVERITY_FATAL,
            .source_category = ALARM_SOURCE_DIGITAL_INPUT,
            .trigger_value = 1.0f,
            .timestamp_ms = 12000
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &alarm_evt), 0);
    k_sleep(K_MSEC(20));

    /* Request flush */
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    ao_datalogger_stats_t stats;
    zassert_equal(ao_datalogger_get_stats(&s_datalogger, &stats), 0);
    zassert_true(stats.events_written >= 1);

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}

ZTEST(ao_datalogger_suite, test_system_event_boot_staging_and_time_sync)
{
    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    /* Mark storage as mounted (unsynced time initially) */
    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    (void)ao_datalogger_post_event(&s_datalogger, &mount_evt);

    /* Post system event prior to time sync (goes to boot staging) */
    datalogger_event_envelope_t sys_evt1 = {
        .type = DATALOGGER_EVT_SYSTEM_EVENT,
        .seq_id = 1,
        .uptime_ms = 150,
        .payload.system = {
            .type = SYSTEM_EVENT_BOOT,
            .severity = ALARM_SEVERITY_INFO,
            .subsystem = "BOOT",
            .message = "Cold boot started",
            .timestamp_ms = 150
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &sys_evt1), 0);
    k_sleep(K_MSEC(20));

    /* Emit TIME_SYNC event */
    datalogger_event_envelope_t sync_evt = {
        .type = DATALOGGER_EVT_TIME_SYNC_TRIGGER,
        .seq_id = 2,
        .uptime_ms = 5000,
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &sync_evt), 0);
    k_sleep(K_MSEC(20));

    /* Post system event after time sync (goes to calendar system log) */
    datalogger_event_envelope_t sys_evt2 = {
        .type = DATALOGGER_EVT_SYSTEM_EVENT,
        .seq_id = 3,
        .uptime_ms = 5100,
        .payload.system = {
            .type = SYSTEM_EVENT_NETWORK_UP,
            .severity = ALARM_SEVERITY_INFO,
            .subsystem = "NET",
            .message = "Ethernet link up, IP acquired",
            .timestamp_ms = 5100
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &sys_evt2), 0);
    k_sleep(K_MSEC(20));

    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    ao_datalogger_stats_t stats;
    zassert_equal(ao_datalogger_get_stats(&s_datalogger, &stats), 0);
    zassert_true(stats.events_written >= 2);

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}
