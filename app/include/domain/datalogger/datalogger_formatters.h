/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file datalogger_formatters.h
 * @brief Pure C99 ASCII CSV and Text Formatters for Historical Telemetry & Events.
 *
 * Zero Zephyr RTOS or vendor hardware dependencies.
 */

#ifndef APP_DOMAIN_DATALOGGER_DATALOGGER_FORMATTERS_H_
#define APP_DOMAIN_DATALOGGER_DATALOGGER_FORMATTERS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/datalogger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATALOGGER_CSV_HEADER_TEMPERATURE \
    "# schema_version=1;source=BrightC_V3;type=temperature_history\n" \
    "Timestamp_ISO,Epoch_s,HVAC_Mode,Active_Mask,T_ctrl_avg,T_ctrl_max,T_ctrl_min," \
    "T_ctrl_g0,T_ctrl_g1,T_inj_mean,T_ret_mean,Delta_T_clima\n"

#define DATALOGGER_CSV_HEADER_ENERGY \
    "# schema_version=1;source=BrightC_V3;type=energy_history\n" \
    "Timestamp_ISO,Epoch_s,V_a,V_b,V_c,I_a,I_b,I_c,P_tot_kW,PF,Frequency_Hz,Energy_kWh,Reactive_Energy_kvarh\n"

#define DATALOGGER_CSV_HEADER_THERMAL \
    "# schema_version=1;source=BrightC_V3;type=thermal_history\n" \
    "Timestamp_ISO,Epoch_s,Q_sens_kW,Q_lat_kW,Q_tot_kW,COP_real,COP_Carnot,Eta_II_pct,AirFlow_kg_s,T_dew_C,SHF,Alarm_Bits\n"

#define DATALOGGER_CSV_HEADER_TELEMETRY \
    "# schema_version=2;source=BrightC_V3;type=periodic_telemetry\n" \
    "Timestamp_ISO,Epoch_s,HVAC_Mode,Active_Mask,T_ctrl_avg,T_ctrl_max,T_ctrl_min," \
    "T_ctrl_g0,T_ctrl_g1,T_inj_mean,T_ret_mean,Delta_T_clima," \
    "V_a,V_b,V_c,I_a,I_b,I_c,P_tot_kW,PF,Energy_kWh,Q_sens_kW,Q_lat_kW,COP_real,AirFlow_kg_s,T_dew_C,Alarm_Bits\n"

#define DATALOGGER_CSV_HEADER_ALARMS \
    "# schema_version=2;source=BrightC_V3;type=alarm_soe\n" \
    "Timestamp_ISO,Uptime_ms,Seq_ID,Alarm_ID,Subsystem,Transition,Threshold,Measured,Severity,Tag\n"

#define DATALOGGER_CSV_HEADER_BOOT \
    "# schema_version=1;source=BrightC_V3;type=boot_staging\n" \
    "Uptime_ms,Seq_ID,Stream,Event_ID,Message\n"

/**
 * @brief Formats a dedicated periodic temperature record into a CSV row.
 *
 * @param[in]  rec      Temperature record data.
 * @param[in]  iso_time Pre-formatted ISO 8601 timestamp string.
 * @param[out] out_str  Destination character buffer.
 * @param[in]  out_size Maximum size of the destination buffer.
 * @return Number of characters written (excluding null terminator), or negative errno on error.
 */
int datalogger_format_temperature_row(const datalogger_temperature_record_t *rec,
                                      const char *iso_time,
                                      char *out_str,
                                      size_t out_size);

/**
 * @brief Formats a dedicated periodic electrical energy record into a CSV row.
 *
 * @param[in]  rec      Energy record data.
 * @param[in]  iso_time Pre-formatted ISO 8601 timestamp string.
 * @param[out] out_str  Destination character buffer.
 * @param[in]  out_size Maximum size of the destination buffer.
 * @return Number of characters written (excluding null terminator), or negative errno on error.
 */
int datalogger_format_energy_row(const datalogger_energy_record_t *rec,
                                 const char *iso_time,
                                 char *out_str,
                                 size_t out_size);

/**
 * @brief Formats a dedicated periodic thermal physics record into a CSV row.
 *
 * @param[in]  rec      Thermal physics record data.
 * @param[in]  iso_time Pre-formatted ISO 8601 timestamp string.
 * @param[out] out_str  Destination character buffer.
 * @param[in]  out_size Maximum size of the destination buffer.
 * @return Number of characters written (excluding null terminator), or negative errno on error.
 */
int datalogger_format_thermal_row(const datalogger_thermal_record_t *rec,
                                  const char *iso_time,
                                  char *out_str,
                                  size_t out_size);

/**
 * @brief Formats a unified periodic telemetry record into a CSV row.
 *
 * @param[in]  rec      Telemetry record data.
 * @param[in]  iso_time Pre-formatted ISO 8601 timestamp string (e.g. "2026-11-12T14:30:00.000Z").
 * @param[out] out_str  Destination character buffer.
 * @param[in]  out_size Maximum size of the destination buffer.
 * @return Number of characters written (excluding null terminator), or negative errno on error.
 */
int datalogger_format_telemetry_row(const datalogger_telemetry_record_t *rec,
                                    const char *iso_time,
                                    char *out_str,
                                    size_t out_size);

/**
 * @brief Formats an alarm transition event into a forensic SOE CSV row.
 *
 * @param[in]  alm       Alarm transition event.
 * @param[in]  seq_id    Monotonic event sequence ID.
 * @param[in]  uptime_ms Hardware uptime at transition.
 * @param[in]  iso_time  Pre-formatted ISO 8601 timestamp string.
 * @param[out] out_str   Destination character buffer.
 * @param[in]  out_size  Maximum size of the destination buffer.
 * @return Number of characters written, or negative errno on error.
 */
int datalogger_format_alarm_row(const alarm_transition_event_t *alm,
                                uint32_t seq_id,
                                uint64_t uptime_ms,
                                const char *iso_time,
                                char *out_str,
                                size_t out_size);

/**
 * @brief Formats an operational system event into a log row.
 *
 * @param[in]  sys       System event message.
 * @param[in]  seq_id    Monotonic event sequence ID.
 * @param[in]  uptime_ms Hardware uptime at event.
 * @param[in]  iso_time  Pre-formatted ISO 8601 timestamp string.
 * @param[out] out_str   Destination character buffer.
 * @param[in]  out_size  Maximum size of the destination buffer.
 * @return Number of characters written, or negative errno on error.
 */
int datalogger_format_system_row(const system_event_msg_t *sys,
                                 uint32_t seq_id,
                                 uint64_t uptime_ms,
                                 const char *iso_time,
                                 char *out_str,
                                 size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_DATALOGGER_DATALOGGER_FORMATTERS_H_ */
