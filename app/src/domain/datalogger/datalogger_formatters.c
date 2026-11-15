/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file datalogger_formatters.c
 * @brief Pure C99 ASCII CSV and Text Formatters Implementation.
 */

#include "domain/datalogger/datalogger_formatters.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *severity_to_str(alarm_severity_t sev)
{
    switch (sev) {
    case ALARM_SEVERITY_INFO:     return "INF";
    case ALARM_SEVERITY_WARNING:  return "WRN";
    case ALARM_SEVERITY_CRITICAL: return "CRT";
    case ALARM_SEVERITY_FATAL:    return "FTL";
    default:                      return "UNK";
    }
}

static const char *transition_to_str(alarm_transition_type_t t)
{
    switch (t) {
    case ALARM_EVENT_ACTIVATED:    return "TRIP";
    case ALARM_EVENT_CLEARED:      return "CLEAR";
    case ALARM_EVENT_ACKNOWLEDGED: return "ACK";
    case ALARM_EVENT_LATCHED:      return "LATCH";
    default:                       return "UNKNOWN";
    }
}

static const char *subsystem_to_str(alarm_source_category_t cat)
{
    switch (cat) {
    case ALARM_SOURCE_DIGITAL_INPUT: return "DIGITAL_IN";
    case ALARM_SOURCE_SENSOR_ANALOG: return "SENSORS";
    case ALARM_SOURCE_ELECTRICAL:    return "POWER";
    case ALARM_SOURCE_HVAC_CONTROL:  return "HVAC";
    case ALARM_SOURCE_SYSTEM_INFRA:  return "SYSTEM";
    default:                         return "CUSTOM";
    }
}

int datalogger_format_telemetry_row(const datalogger_telemetry_record_t *rec,
                                    const char *iso_time,
                                    char *out_str,
                                    size_t out_size)
{
    if (rec == NULL || iso_time == NULL || out_str == NULL || out_size == 0U) {
        return -EINVAL;
    }

    int written = snprintf(out_str, out_size,
        "%s,%lld,%u,0x%02X,%.2f,%.2f,%.2f,"
        "%.2f,%.2f,%.2f,%.2f,%.2f,"
        "%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.3f,%.2f,%.2f,"
        "%.2f,%.2f,%.2f,%.3f,%.2f,0x%08X\n",
        iso_time,
        (long long)rec->epoch_s,
        (unsigned int)rec->hvac_mode,
        (unsigned int)rec->active_unit_mask,
        (double)rec->t_ctrl_avg,
        (double)rec->t_ctrl_max,
        (double)rec->t_ctrl_min,
        (double)rec->t_ctrl_g0,
        (double)rec->t_ctrl_g1,
        (double)rec->t_inj_mean,
        (double)rec->t_ret_mean,
        (double)rec->delta_t_clima,
        (double)rec->v_a,
        (double)rec->v_b,
        (double)rec->v_c,
        (double)rec->i_a,
        (double)rec->i_b,
        (double)rec->i_c,
        (double)rec->p_tot_kw,
        (double)rec->power_factor,
        (double)rec->energy_kwh,
        (double)rec->q_sens_kw,
        (double)rec->q_lat_kw,
        (double)rec->cop_real,
        (double)rec->airflow_kg_s,
        (double)rec->t_dew_c,
        (unsigned int)rec->alarm_bits);

    if (written < 0 || (size_t)written >= out_size) {
        return -ENOMEM;
    }

    return written;
}

int datalogger_format_alarm_row(const alarm_transition_event_t *alm,
                                uint32_t seq_id,
                                uint64_t uptime_ms,
                                const char *iso_time,
                                char *out_str,
                                size_t out_size)
{
    if (alm == NULL || iso_time == NULL || out_str == NULL || out_size == 0U) {
        return -EINVAL;
    }

    int written = snprintf(out_str, out_size,
        "%s,%llu,%u,%u,%s,%s,%.2f,%.2f,%s,%u\n",
        iso_time,
        (unsigned long long)uptime_ms,
        (unsigned int)seq_id,
        (unsigned int)alm->alarm_id,
        subsystem_to_str(alm->source_category),
        transition_to_str(alm->event_type),
        (double)0.0f,
        (double)alm->trigger_value,
        severity_to_str(alm->severity),
        (unsigned int)alm->alarm_id);

    if (written < 0 || (size_t)written >= out_size) {
        return -ENOMEM;
    }

    return written;
}

int datalogger_format_system_row(const system_event_msg_t *sys,
                                 uint32_t seq_id,
                                 uint64_t uptime_ms,
                                 const char *iso_time,
                                 char *out_str,
                                 size_t out_size)
{
    if (sys == NULL || iso_time == NULL || out_str == NULL || out_size == 0U) {
        return -EINVAL;
    }

    int written = snprintf(out_str, out_size,
        "%s [UPTIME:%llu] [SEQ:%u] [%s] [%s] %s\n",
        iso_time,
        (unsigned long long)uptime_ms,
        (unsigned int)seq_id,
        severity_to_str(sys->severity),
        sys->subsystem[0] != '\0' ? sys->subsystem : "SYS",
        sys->message);

    if (written < 0 || (size_t)written >= out_size) {
        return -ENOMEM;
    }

    return written;
}
