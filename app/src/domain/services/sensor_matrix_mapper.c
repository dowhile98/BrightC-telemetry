/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor matrix domain ingestion, calibration and mapping engine.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#include "sensor_matrix_mapper.h"
#include "transducer_rtd.h"
#include "transducer_current_loop.h"

#include <errno.h>
#include <string.h>

int sensor_matrix_init_default_pilot_config(sensor_channel_cfg_t *configs, size_t max_entries)
{
    if (configs == NULL || max_entries < SENSOR_PHYS_CH_MAX) {
        return -EINVAL;
    }

    memset(configs, 0, sizeof(sensor_channel_cfg_t) * max_entries);

    /* Pilot Site HVAC Unit Distribution:
     * Pairs 0 and 1 -> HVAC Unit 1
     * Pairs 2 and 3 -> HVAC Unit 2
     * Pairs 4 and 5 -> HVAC Unit 3
     */
    static const uint8_t s_pair_units[6] = {1, 1, 2, 2, 3, 3};

    /* 1. External CWT CH1 to CH6: Supply Air Probes (Pairs 0 to 5) */
    for (uint8_t k = 0; k < 6; k++) {
        configs[k].physical_ch = (uint8_t)(PHYS_CH_CWT_1 + k);
        configs[k].role = (uint8_t)SENSOR_ROLE_TEMP_SUPPLY_AIR;
        configs[k].target_index = k;
        configs[k].cal_offset_c_centi = 0;
        configs[k].cal_gain_ppm = 0;
        configs[k].associated_hvac_unit = s_pair_units[k];
    }

    /* 2. External CWT CH7 to CH12: Return Air Probes (Pairs 0 to 5) */
    for (uint8_t k = 0; k < 6; k++) {
        uint8_t idx = 6 + k;
        configs[idx].physical_ch = (uint8_t)(PHYS_CH_CWT_1 + idx);
        configs[idx].role = (uint8_t)SENSOR_ROLE_TEMP_RETURN_AIR;
        configs[idx].target_index = k;
        configs[idx].cal_offset_c_centi = 0;
        configs[idx].cal_gain_ppm = 0;
        configs[idx].associated_hvac_unit = s_pair_units[k];
    }

    /* 3. External CWT CH13 and CH14: Room Ambient Probes (0 and 1) */
    configs[12].physical_ch = (uint8_t)PHYS_CH_CWT_13;
    configs[12].role = (uint8_t)SENSOR_ROLE_TEMP_ROOM_AMBIENT;
    configs[12].target_index = 0;
    configs[12].cal_offset_c_centi = 0;
    configs[12].cal_gain_ppm = 0;
    configs[12].associated_hvac_unit = 0;

    configs[13].physical_ch = (uint8_t)PHYS_CH_CWT_14;
    configs[13].role = (uint8_t)SENSOR_ROLE_TEMP_ROOM_AMBIENT;
    configs[13].target_index = 1;
    configs[13].cal_offset_c_centi = 0;
    configs[13].cal_gain_ppm = 0;
    configs[13].associated_hvac_unit = 0;

    /* 4. External CWT CH15 and CH16: Outdoor Ambient Probes (0 and 1) */
    configs[14].physical_ch = (uint8_t)PHYS_CH_CWT_15;
    configs[14].role = (uint8_t)SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT;
    configs[14].target_index = 0;
    configs[14].cal_offset_c_centi = 0;
    configs[14].cal_gain_ppm = 0;
    configs[14].associated_hvac_unit = 0;

    configs[15].physical_ch = (uint8_t)PHYS_CH_CWT_16;
    configs[15].role = (uint8_t)SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT;
    configs[15].target_index = 1;
    configs[15].cal_offset_c_centi = 0;
    configs[15].cal_gain_ppm = 0;
    configs[15].associated_hvac_unit = 0;

    /* 5. Internal On-Board RTD 1 to 5: Disabled by default (100% spare reserve) */
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t idx = 16 + i;
        configs[idx].physical_ch = (uint8_t)(PHYS_CH_INTERNAL_RTD_1 + i);
        configs[idx].role = (uint8_t)SENSOR_ROLE_DISABLED;
        configs[idx].target_index = 0;
        configs[idx].cal_offset_c_centi = 0;
        configs[idx].cal_gain_ppm = 0;
        configs[idx].associated_hvac_unit = 0;
    }

    /* 6. Analog 4-20 mA J9 CH1: Return Grille Humidity (HR_ret) */
    configs[21].physical_ch = (uint8_t)PHYS_CH_ANALOG_J9_CH1;
    configs[21].role = (uint8_t)SENSOR_ROLE_HUMIDITY_RETURN;
    configs[21].target_index = 0;
    configs[21].cal_offset_c_centi = 0;
    configs[21].cal_gain_ppm = 0;
    configs[21].associated_hvac_unit = 0;

    /* 7. Analog 4-20 mA J9 CH2 to CH10: Disabled by default (Phase 2 Reserve) */
    for (uint8_t i = 1; i < 10; i++) {
        uint8_t idx = 21 + i;
        configs[idx].physical_ch = (uint8_t)(PHYS_CH_ANALOG_J9_CH1 + i);
        configs[idx].role = (uint8_t)SENSOR_ROLE_DISABLED;
        configs[idx].target_index = 0;
        configs[idx].cal_offset_c_centi = 0;
        configs[idx].cal_gain_ppm = 0;
        configs[idx].associated_hvac_unit = 0;
    }

    return 0;
}

int sensor_matrix_map(const raw_sensor_readings_t *raw,
                      const sensor_channel_cfg_t *configs,
                      size_t cfg_count,
                      thermal_matrix_frame_t *out_frame)
{
    if (raw == NULL || configs == NULL || out_frame == NULL) {
        return -EINVAL;
    }

    memset(out_frame, 0, sizeof(thermal_matrix_frame_t));
    out_frame->timestamp_us = raw->timestamp_us;

    /* Tracking state for pair consensus */
    bool pair_inj_configured[SENSOR_MATRIX_MAX_PAIRS] = {false};
    bool pair_ret_configured[SENSOR_MATRIX_MAX_PAIRS] = {false};
    bool pair_inj_valid[SENSOR_MATRIX_MAX_PAIRS] = {false};
    bool pair_ret_valid[SENSOR_MATRIX_MAX_PAIRS] = {false};
    bool pair_inj_open[SENSOR_MATRIX_MAX_PAIRS] = {false};
    bool pair_ret_open[SENSOR_MATRIX_MAX_PAIRS] = {false};

    for (size_t i = 0; i < cfg_count; i++) {
        const sensor_channel_cfg_t *cfg = &configs[i];

        if (cfg->role == (uint8_t)SENSOR_ROLE_DISABLED) {
            continue;
        }

        if (!physical_channel_is_valid((physical_channel_id_t)cfg->physical_ch)) {
            continue;
        }

        float raw_val = 0.0f;
        float current_ma = 0.0f;
        bool is_valid = false;
        bool is_open = false;

        /* A. External CWT Modbus Concentrator Channels */
        if (cfg->physical_ch >= PHYS_CH_CWT_1 && cfg->physical_ch <= PHYS_CH_CWT_16) {
            uint8_t cwt_idx = cfg->physical_ch - PHYS_CH_CWT_1;

            if (!raw->cwt_comm_ok) {
                is_valid = false;
                is_open = true;
            } else {
                is_open = (raw->cwt_open_probe_mask & (1U << cwt_idx)) != 0;
                bool channel_valid = (raw->cwt_valid_mask & (1U << cwt_idx)) != 0;
                raw_val = raw->cwt_temperatures_c[cwt_idx];

                if (!channel_valid || is_open ||
                    raw_val < SENSOR_TEMP_MIN_VALID_C || raw_val > SENSOR_TEMP_MAX_VALID_C) {
                    is_valid = false;
                    is_open = true;
                } else {
                    /* Apply 2-point calibration */
                    float gain = sensor_cal_gain_to_factor(cfg->cal_gain_ppm);
                    float offset = sensor_cal_offset_to_c(cfg->cal_offset_c_centi);
                    raw_val = (raw_val * gain) + offset;

                    /* Re-verify post-calibration physical sanity */
                    if (raw_val >= SENSOR_TEMP_MIN_VALID_C && raw_val <= SENSOR_TEMP_MAX_VALID_C) {
                        is_valid = true;
                        is_open = false;
                    } else {
                        is_valid = false;
                    }
                }
            }
        }
        /* B. Internal On-Board RTD Stage with INA826 (Channels 1 to 5) */
        else if (cfg->physical_ch >= PHYS_CH_INTERNAL_RTD_1 && cfg->physical_ch <= PHYS_CH_INTERNAL_RTD_5) {
            uint8_t rtd_idx = cfg->physical_ch - PHYS_CH_INTERNAL_RTD_1;
            bool adc_valid = (raw->internal_rtd_valid_mask & (1U << rtd_idx)) != 0;

            if (!adc_valid) {
                is_valid = false;
                is_open = true;
            } else {
                float adc_counts = raw->internal_rtd_raw_adc[rtd_idx];
                float temp_c = 0.0f;
                int cvd_res = transducer_rtd_adc_to_celsius(NULL, adc_counts, &temp_c);

                if (cvd_res != 0 || temp_c < SENSOR_TEMP_MIN_VALID_C || temp_c > SENSOR_TEMP_MAX_VALID_C) {
                    is_valid = false;
                    is_open = true;
                } else {
                    /* Apply 2-point calibration */
                    float gain = sensor_cal_gain_to_factor(cfg->cal_gain_ppm);
                    float offset = sensor_cal_offset_to_c(cfg->cal_offset_c_centi);
                    raw_val = (temp_c * gain) + offset;

                    if (raw_val >= SENSOR_TEMP_MIN_VALID_C && raw_val <= SENSOR_TEMP_MAX_VALID_C) {
                        is_valid = true;
                        is_open = false;
                    } else {
                        is_valid = false;
                    }
                }
            }
        }
        /* C. Analog 4-20 mA Inputs on Connector J9 (Channels 1 to 10) */
        else if (cfg->physical_ch >= PHYS_CH_ANALOG_J9_CH1 && cfg->physical_ch <= PHYS_CH_ANALOG_J9_CH10) {
            uint8_t j9_idx = cfg->physical_ch - PHYS_CH_ANALOG_J9_CH1;
            bool adc_valid = (raw->analog_j9_valid_mask & (1U << j9_idx)) != 0;

            if (!adc_valid) {
                is_valid = false;
            } else {
                float adc_counts = raw->analog_j9_raw_adc[j9_idx];
                int loop_res = transducer_current_loop_adc_to_ma(NULL, adc_counts, &current_ma);

                if (loop_res != 0 ||
                    current_ma < SENSOR_4_20MA_LOOP_MIN_MA ||
                    current_ma > SENSOR_4_20MA_LOOP_MAX_MA) {
                    is_valid = false;
                } else {
                    is_valid = true;
                }
            }
        }

        /* Route measurement into semantic target based on configured role */
        switch ((sensor_role_t)cfg->role) {
        case SENSOR_ROLE_TEMP_SUPPLY_AIR: {
            uint8_t k = cfg->target_index;
            if (k < SENSOR_MATRIX_MAX_PAIRS) {
                pair_inj_configured[k] = true;
                pair_inj_valid[k] = is_valid;
                pair_inj_open[k] = is_open;
                out_frame->t_inj[k] = raw_val;
                out_frame->pair_hvac_unit_map[k] = cfg->associated_hvac_unit;
                if ((k + 1U) > out_frame->configured_pairs) {
                    out_frame->configured_pairs = k + 1U;
                }
            }
            break;
        }

        case SENSOR_ROLE_TEMP_RETURN_AIR: {
            uint8_t k = cfg->target_index;
            if (k < SENSOR_MATRIX_MAX_PAIRS) {
                pair_ret_configured[k] = true;
                pair_ret_valid[k] = is_valid;
                pair_ret_open[k] = is_open;
                out_frame->t_ret[k] = raw_val;
                if (cfg->associated_hvac_unit != 0) {
                    out_frame->pair_hvac_unit_map[k] = cfg->associated_hvac_unit;
                }
                if ((k + 1U) > out_frame->configured_pairs) {
                    out_frame->configured_pairs = k + 1U;
                }
            }
            break;
        }

        case SENSOR_ROLE_TEMP_ROOM_AMBIENT: {
            uint8_t m = cfg->target_index;
            if (m < SENSOR_MATRIX_MAX_ROOM_PROBES) {
                out_frame->t_room[m] = raw_val;
                if (is_valid && !is_open) {
                    out_frame->room_valid_mask |= (uint8_t)(1U << m);
                }
                if ((m + 1U) > out_frame->configured_room_probes) {
                    out_frame->configured_room_probes = m + 1U;
                }
            }
            break;
        }

        case SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT: {
            uint8_t l = cfg->target_index;
            if (l < SENSOR_MATRIX_MAX_OUTDOOR_PROBES) {
                out_frame->t_outdoor[l] = raw_val;
                if (is_valid && !is_open) {
                    out_frame->outdoor_valid_mask |= (uint8_t)(1U << l);
                }
                if ((l + 1U) > out_frame->configured_outdoor_probes) {
                    out_frame->configured_outdoor_probes = l + 1U;
                }
            }
            break;
        }

        case SENSOR_ROLE_HUMIDITY_RETURN: {
            if (is_valid) {
                /* Linear 4-20 mA conversion to 0.0 - 100.0 % */
                float hr = ((current_ma - 4.0f) / 16.0f) * 100.0f;
                float gain = sensor_cal_gain_to_factor(cfg->cal_gain_ppm);
                float offset = sensor_cal_offset_to_c(cfg->cal_offset_c_centi);
                hr = (hr * gain) + offset;

                if (hr < 0.0f) {
                    hr = 0.0f;
                } else if (hr > 100.0f) {
                    hr = 100.0f;
                }
                out_frame->humidity_return_pct = hr;
                out_frame->humidity_return_valid = true;
            } else {
                out_frame->humidity_return_pct = 0.0f;
                out_frame->humidity_return_valid = false;
            }
            break;
        }

        case SENSOR_ROLE_PRESSURE_REFRIGERANT: {
            if (is_valid) {
                /* Linear 4-20 mA conversion to 0.0 - 45.0 bar */
                float p = ((current_ma - 4.0f) / 16.0f) * 45.0f;
                float gain = sensor_cal_gain_to_factor(cfg->cal_gain_ppm);
                float offset = sensor_cal_offset_to_c(cfg->cal_offset_c_centi);
                p = (p * gain) + offset;

                if (p < 0.0f) {
                    p = 0.0f;
                } else if (p > 45.0f) {
                    p = 45.0f;
                }
                out_frame->refrigerant_pressure_bar = p;
                out_frame->refrigerant_pressure_valid = true;
            } else {
                out_frame->refrigerant_pressure_bar = 0.0f;
                out_frame->refrigerant_pressure_valid = false;
            }
            break;
        }

        case SENSOR_ROLE_PRESSURE_AIR_FILTER: {
            if (is_valid) {
                /* Linear 4-20 mA conversion to 0.0 - 500.0 Pa */
                float dp = ((current_ma - 4.0f) / 16.0f) * 500.0f;
                float gain = sensor_cal_gain_to_factor(cfg->cal_gain_ppm);
                float offset = sensor_cal_offset_to_c(cfg->cal_offset_c_centi);
                dp = (dp * gain) + offset;

                if (dp < 0.0f) {
                    dp = 0.0f;
                } else if (dp > 500.0f) {
                    dp = 500.0f;
                }
                out_frame->filter_diff_pressure_pa = dp;
                out_frame->filter_diff_pressure_valid = true;
            } else {
                out_frame->filter_diff_pressure_pa = 0.0f;
                out_frame->filter_diff_pressure_valid = false;
            }
            break;
        }

        default:
            break;
        }
    }

    /* Compute atomic pair health and consensus masks (§6.2) */
    for (uint8_t k = 0; k < out_frame->configured_pairs; k++) {
        bool configured = pair_inj_configured[k] || pair_ret_configured[k];
        if (!configured) {
            continue;
        }

        /* Probe open mask: asserted if either probe is open, disconnected or unassigned */
        if (pair_inj_open[k] || pair_ret_open[k] ||
            !pair_inj_configured[k] || !pair_ret_configured[k]) {
            out_frame->pair_open_probe_mask |= (uint16_t)(1U << k);
        }

        /* Consensus valid mask: asserted ONLY if both probes are present, valid and NOT open */
        if (pair_inj_configured[k] && pair_ret_configured[k] &&
            pair_inj_valid[k] && pair_ret_valid[k] &&
            !pair_inj_open[k] && !pair_ret_open[k]) {
            out_frame->pair_valid_mask |= (uint16_t)(1U << k);
        }
    }

    return 0;
}
