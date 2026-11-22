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

ZTEST(ao_datalogger_suite, test_telemetry_stream_logging)
{
    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    /* Mount SD storage */
    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &mount_evt), 0);

    /* Set date */
    zassert_equal(ao_datalogger_set_date(&s_datalogger, "2026-11-12"), 0);
    k_sleep(K_MSEC(20));

    /* Post unified telemetry sample */
    datalogger_event_envelope_t tel_evt = {
        .type = DATALOGGER_EVT_TELEMETRY_SAMPLE,
        .seq_id = 10,
        .uptime_ms = 120000,
        .payload.telemetry = {
            .epoch_s = 1794474862,
            .uptime_ms = 120000,
            .hvac_mode = 1,
            .active_unit_mask = 0x05,
            .t_ctrl_avg = 23.50f,
            .t_ctrl_max = 24.10f,
            .t_ctrl_min = 22.80f,
            .v_a = 220.0f,
            .v_b = 221.0f,
            .v_c = 219.5f,
            .i_a = 12.30f,
            .i_b = 11.90f,
            .i_c = 12.10f,
            .p_tot_kw = 7.500f,
            .power_factor = 0.96f,
            .energy_kwh = 3450.25f,
            .q_sens_kw = 10.50f,
            .q_lat_kw = 1.80f,
            .cop_real = 3.40f,
            .airflow_kg_s = 0.750f,
            .t_dew_c = 13.50f,
            .alarm_bits = 0x00000000
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &tel_evt), 0);
    k_sleep(K_MSEC(20));

    /* Flush to commit staging buffer to fake FS */
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    ao_datalogger_stats_t stats;
    zassert_equal(ao_datalogger_get_stats(&s_datalogger, &stats), 0);
    zassert_true(stats.events_written >= 1);

    /* Verify file creation and content in fake filesystem */
    fake_storage_fs_state_t *st = fake_storage_fs_get_state();
    bool found_file = false;
    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_NODES; i++) {
        if (st->nodes[i].exists && strstr(st->nodes[i].path, "2026-11-12_telemetry.csv") != NULL) {
            found_file = true;
            zassert_true(st->nodes[i].size > 0);
            /* Check header and row presence */
            zassert_not_null(strstr((const char *)st->nodes[i].data, "# schema_version=2;source=BrightC_V3;type=periodic_telemetry"));
            zassert_not_null(strstr((const char *)st->nodes[i].data, "23.50,24.10,22.80"));
            break;
        }
    }
    zassert_true(found_file, "Telemetry file not found in fake storage nodes!");

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}

ZTEST(ao_datalogger_suite, test_telemetry_stream_date_rollover)
{
    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &mount_evt), 0);

    /* Day 1 */
    zassert_equal(ao_datalogger_set_date(&s_datalogger, "2026-11-12"), 0);
    k_sleep(K_MSEC(20));

    datalogger_event_envelope_t tel_evt1 = {
        .type = DATALOGGER_EVT_TELEMETRY_SAMPLE,
        .seq_id = 1,
        .uptime_ms = 1000,
        .payload.telemetry = {
            .epoch_s = 1794474862,
            .t_ctrl_avg = 20.0f
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &tel_evt1), 0);
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    /* Day 2 rollover */
    zassert_equal(ao_datalogger_set_date(&s_datalogger, "2026-11-13"), 0);
    k_sleep(K_MSEC(20));

    datalogger_event_envelope_t tel_evt2 = {
        .type = DATALOGGER_EVT_TELEMETRY_SAMPLE,
        .seq_id = 2,
        .uptime_ms = 86401000,
        .payload.telemetry = {
            .epoch_s = 1794561262,
            .t_ctrl_avg = 25.0f
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &tel_evt2), 0);
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    fake_storage_fs_state_t *st = fake_storage_fs_get_state();
    bool found_day1 = false;
    bool found_day2 = false;

    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_NODES; i++) {
        if (st->nodes[i].exists) {
            if (strstr(st->nodes[i].path, "2026/11/2026-11-12_telemetry.csv") != NULL) {
                found_day1 = true;
            }
            if (strstr(st->nodes[i].path, "2026/11/2026-11-13_telemetry.csv") != NULL) {
                found_day2 = true;
            }
        }
    }
    zassert_true(found_day1, "Day 1 telemetry file not found!");
    zassert_true(found_day2, "Day 2 telemetry file not found!");

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}

ZTEST(ao_datalogger_suite, test_multi_stream_partitioned_logging)
{
    zassert_equal(ao_datalogger_start(&s_datalogger), 0);

    /* Mount SD storage */
    datalogger_event_envelope_t mount_evt = {
        .type = DATALOGGER_EVT_STORAGE_STATUS,
        .payload.storage_status = STORAGE_FS_CARD_MOUNTED,
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &mount_evt), 0);
    zassert_equal(ao_datalogger_set_date(&s_datalogger, "2026-11-12"), 0);
    k_sleep(K_MSEC(20));

    /* 1. Post Temperature Sample */
    datalogger_event_envelope_t temp_evt = {
        .type = DATALOGGER_EVT_TEMPERATURE_SAMPLE,
        .seq_id = 1,
        .uptime_ms = 10000,
        .payload.temperature = {
            .epoch_s = 1794474862,
            .uptime_ms = 10000,
            .hvac_mode = 1,
            .active_unit_mask = 0x01,
            .t_ctrl_avg = 22.4f,
            .t_ctrl_max = 23.0f,
            .t_ctrl_min = 21.8f,
            .t_ctrl_g0 = 22.5f,
            .t_ctrl_g1 = 22.3f,
            .t_inj_mean = 13.5f,
            .t_ret_mean = 23.5f,
            .delta_t_clima = 10.0f
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &temp_evt), 0);

    /* 2. Post Energy Sample */
    datalogger_event_envelope_t energy_evt = {
        .type = DATALOGGER_EVT_ENERGY_SAMPLE,
        .seq_id = 2,
        .uptime_ms = 10000,
        .payload.energy = {
            .epoch_s = 1794474862,
            .uptime_ms = 10000,
            .v_a = 220.0f,
            .v_b = 220.5f,
            .v_c = 219.8f,
            .i_a = 15.2f,
            .i_b = 14.8f,
            .i_c = 15.0f,
            .p_tot_kw = 9.8f,
            .power_factor = 0.95f,
            .frequency_hz = 60.0f,
            .energy_kwh = 12500.5f,
            .reactive_energy_kvarh = 320.0f
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &energy_evt), 0);

    /* 3. Post Thermal Sample */
    datalogger_event_envelope_t thermal_evt = {
        .type = DATALOGGER_EVT_THERMAL_SAMPLE,
        .seq_id = 3,
        .uptime_ms = 10000,
        .payload.thermal = {
            .epoch_s = 1794474862,
            .uptime_ms = 10000,
            .q_sens_kw = 12.5f,
            .q_lat_kw = 2.1f,
            .q_tot_kw = 14.6f,
            .cop_real = 3.6f,
            .cop_carnot = 5.5f,
            .eta_ii_pct = 68.2f,
            .airflow_kg_s = 0.82f,
            .t_dew_c = 12.8f,
            .shf = 0.856f,
            .alarm_bits = 0x00000000
        }
    };
    zassert_equal(ao_datalogger_post_event(&s_datalogger, &thermal_evt), 0);
    k_sleep(K_MSEC(20));

    /* Flush streams */
    zassert_equal(ao_datalogger_flush(&s_datalogger), 0);
    k_sleep(K_MSEC(50));

    /* Inspect fake FS nodes */
    fake_storage_fs_state_t *st = fake_storage_fs_get_state();
    bool found_temp = false;
    bool found_energy = false;
    bool found_thermal = false;

    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_NODES; i++) {
        if (st->nodes[i].exists) {
            if (strstr(st->nodes[i].path, "history/temperature/2026/11/2026-11-12_temperature.csv") != NULL) {
                found_temp = true;
                zassert_not_null(strstr((const char *)st->nodes[i].data, "# schema_version=1;source=BrightC_V3;type=temperature_history"));
                zassert_not_null(strstr((const char *)st->nodes[i].data, "22.40,23.00,21.80"));
            }
            if (strstr(st->nodes[i].path, "history/energy/2026/11/2026-11-12_energy.csv") != NULL) {
                found_energy = true;
                zassert_not_null(strstr((const char *)st->nodes[i].data, "# schema_version=1;source=BrightC_V3;type=energy_history"));
                zassert_not_null(strstr((const char *)st->nodes[i].data, "220.0,220.5,219.8"));
            }
            if (strstr(st->nodes[i].path, "history/thermal/2026/11/2026-11-12_thermal.csv") != NULL) {
                found_thermal = true;
                zassert_not_null(strstr((const char *)st->nodes[i].data, "# schema_version=1;source=BrightC_V3;type=thermal_history"));
                zassert_not_null(strstr((const char *)st->nodes[i].data, "12.50,2.10,14.60"));
            }
        }
    }

    zassert_true(found_temp, "Temperature history file missing or misplaced!");
    zassert_true(found_energy, "Energy history file missing or misplaced!");
    zassert_true(found_thermal, "Thermal history file missing or misplaced!");

    (void)ao_datalogger_stop(&s_datalogger);
    (void)ao_datalogger_wait_stopped(&s_datalogger, K_MSEC(1000));
}
