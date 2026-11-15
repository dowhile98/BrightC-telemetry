/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for domain datalogger CSV formatters.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "domain/datalogger/datalogger_formatters.h"

ZTEST_SUITE(datalogger_domain_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(datalogger_domain_suite, test_format_telemetry_row_golden)
{
    datalogger_telemetry_record_t rec = {
        .epoch_s = 1794474862,
        .uptime_ms = 120000,
        .hvac_mode = 2,
        .active_unit_mask = 0x03,
        .t_ctrl_avg = 23.50f,
        .t_ctrl_max = 24.10f,
        .t_ctrl_min = 22.80f,
        .t_ctrl_g0 = 24.10f,
        .t_ctrl_g1 = 23.80f,
        .t_inj_mean = 14.20f,
        .t_ret_mean = 24.50f,
        .delta_t_clima = 10.30f,
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
        .alarm_bits = 0x00000001
    };

    char buf[512];
    int len = datalogger_format_telemetry_row(&rec, "2026-11-12T14:34:22.000Z", buf, sizeof(buf));
    zassert_true(len > 0, "Format failed: %d", len);

    const char *expected = "2026-11-12T14:34:22.000Z,1794474862,2,0x03,23.50,24.10,22.80,"
                           "24.10,23.80,14.20,24.50,10.30,"
                           "220.0,221.0,219.5,12.30,11.90,12.10,7.500,0.96,3450.25,"
                           "10.50,1.80,3.40,0.750,13.50,0x00000001\n";
    zassert_equal(strcmp(buf, expected), 0, "Mismatch in formatted CSV row: got %s vs expected %s", buf, expected);
}

ZTEST(datalogger_domain_suite, test_format_alarm_row_golden)
{
    alarm_transition_event_t alm = {
        .alarm_id = 42,
        .source_category = ALARM_SOURCE_HVAC_CONTROL,
        .event_type = ALARM_EVENT_ACTIVATED,
        .severity = ALARM_SEVERITY_CRITICAL,
        .trigger_value = 55.4f
    };

    char buf[256];
    int len = datalogger_format_alarm_row(&alm, 101, 45000, "2026-11-12T14:35:00.000Z", buf, sizeof(buf));
    zassert_true(len > 0, "Format failed: %d", len);

    zassert_not_null(strstr(buf, "2026-11-12T14:35:00.000Z,45000,101,42,HVAC,TRIP,0.00,55.40,CRT,42"));
}

ZTEST(datalogger_domain_suite, test_format_system_row_golden)
{
    system_event_msg_t sys = {
        .type = SYSTEM_EVENT_STORAGE_MOUNT,
        .severity = ALARM_SEVERITY_INFO,
        .subsystem = "SDMMC",
        .message = "Filesystem mounted successfully",
        .timestamp_ms = 1200
    };

    char buf[256];
    int len = datalogger_format_system_row(&sys, 5, 1200, "2026-11-12T14:30:00.000Z", buf, sizeof(buf));
    zassert_true(len > 0, "Format failed: %d", len);

    zassert_not_null(strstr(buf, "2026-11-12T14:30:00.000Z [UPTIME:1200] [SEQ:5] [INF] [SDMMC] Filesystem mounted successfully"));
}

ZTEST(datalogger_domain_suite, test_format_null_and_bounds_checks)
{
    char buf[32];
    datalogger_telemetry_record_t rec = {0};

    /* NULL check */
    zassert_equal(datalogger_format_telemetry_row(NULL, "2026-11-12T14:34:22.000Z", buf, sizeof(buf)), -EINVAL);
    zassert_equal(datalogger_format_telemetry_row(&rec, NULL, buf, sizeof(buf)), -EINVAL);
    zassert_equal(datalogger_format_telemetry_row(&rec, "2026-11-12T14:34:22.000Z", NULL, sizeof(buf)), -EINVAL);
    zassert_equal(datalogger_format_telemetry_row(&rec, "2026-11-12T14:34:22.000Z", buf, 0), -EINVAL);

    /* Buffer too small */
    zassert_equal(datalogger_format_telemetry_row(&rec, "2026-11-12T14:34:22.000Z", buf, sizeof(buf)), -ENOMEM);
}
