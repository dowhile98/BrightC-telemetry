/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sensor_matrix_mapper.c
 * @brief Normalizer adapter mapping raw hardware readings into thermal_energy_capture_t.
 */

#include "infrastructure/adapters/sensor_matrix_mapper.h"
#include "domain/thermal_energy/humidity_transducer.h"
#include "common/app_defaults.h"

#include <errno.h>
#include <math.h>
#include <string.h>

/* ========================================================================= */
/* Temperature Matrix Normalizer                                             */
/* ========================================================================= */

int sensor_matrix_map_temperatures(const config_thermal_site_t *site_cfg,
                                   const float flat_temps[64],
                                   uint64_t flat_valid_mask,
                                   thermal_energy_capture_t *out_capture)
{
    if (flat_temps == NULL || out_capture == NULL) {
        return -EINVAL;
    }

    if (site_cfg == NULL || !config_thermal_site_is_valid(site_cfg)) {
        site_cfg = app_defaults_get_thermal_site_cfg();
    }

    /* 1. Thermal Pairs Mapping */
    uint8_t n_pairs = site_cfg->configured_pairs_count;
    if (n_pairs == 0U || n_pairs > THERMAL_MAX_PAIRS) {
        n_pairs = 6U;
    }
    out_capture->configured_pairs = n_pairs;

    for (uint8_t k = 0U; k < n_pairs; k++) {
        uint8_t inj_id = site_cfg->pair_mapping_table[k].supply_flat_id;
        uint8_t ret_id = site_cfg->pair_mapping_table[k].return_flat_id;

        /* Validate supply probe */
        if (inj_id >= 1U && inj_id <= 64U) {
            uint8_t inj_idx = inj_id - 1U;
            if ((flat_valid_mask & (1ULL << inj_idx)) != 0ULL) {
                float val = flat_temps[inj_idx];
                if (!isnan(val) && !isinf(val)) {
                    out_capture->t_inj[k] = val;
                    out_capture->inj_valid_mask |= (1U << k);
                }
            }
        }

        /* Validate return probe */
        if (ret_id >= 1U && ret_id <= 64U) {
            uint8_t ret_idx = ret_id - 1U;
            if ((flat_valid_mask & (1ULL << ret_idx)) != 0ULL) {
                float val = flat_temps[ret_idx];
                if (!isnan(val) && !isinf(val)) {
                    out_capture->t_ret[k] = val;
                    out_capture->ret_valid_mask |= (1U << k);
                }
            }
        }

        /* Pair consensus: both probes valid and delta_t plausible */
        bool inj_ok = (out_capture->inj_valid_mask & (1U << k)) != 0U;
        bool ret_ok = (out_capture->ret_valid_mask & (1U << k)) != 0U;
        if (inj_ok && ret_ok) {
            out_capture->pair_valid_mask |= (1U << k);
        }
    }

    /* 2. Room Ambient Probes Mapping */
    uint8_t m_room = site_cfg->configured_room_probes;
    if (m_room == 0U || m_room > THERMAL_MAX_ROOM_PROBES) {
        m_room = 2U;
    }
    out_capture->configured_room_probes = m_room;

    for (uint8_t m = 0U; m < m_room; m++) {
        uint8_t room_id = site_cfg->room_probes_table[m];
        if (room_id >= 1U && room_id <= 64U) {
            uint8_t room_idx = room_id - 1U;
            if ((flat_valid_mask & (1ULL << room_idx)) != 0ULL) {
                float val = flat_temps[room_idx];
                if (!isnan(val) && !isinf(val)) {
                    out_capture->t_room[m] = val;
                    out_capture->room_valid_mask |= (1U << m);
                }
            }
        }
    }

    /* 3. Outdoor Ambient Reference Probes Mapping */
    uint8_t l_outdoor = site_cfg->configured_outdoor_probes;
    if (l_outdoor == 0U || l_outdoor > THERMAL_MAX_OUTDOOR_PROBES) {
        l_outdoor = 2U;
    }
    out_capture->configured_outdoor_probes = l_outdoor;

    for (uint8_t l = 0U; l < l_outdoor; l++) {
        uint8_t outdoor_id = site_cfg->outdoor_probes_table[l];
        if (outdoor_id >= 1U && outdoor_id <= 64U) {
            uint8_t outdoor_idx = outdoor_id - 1U;
            if ((flat_valid_mask & (1ULL << outdoor_idx)) != 0ULL) {
                float val = flat_temps[outdoor_idx];
                if (!isnan(val) && !isinf(val)) {
                    out_capture->t_outdoor[l] = val;
                    out_capture->outdoor_valid_mask |= (1U << l);
                }
            }
        }
    }

    return 0;
}

/* ========================================================================= */
/* Humidity Loop Normalizer (4-20 mA NAMUR NE43)                             */
/* ========================================================================= */

int sensor_matrix_map_humidity(float loop_current_ma,
                               bool is_valid,
                               thermal_energy_capture_t *out_capture)
{
    if (out_capture == NULL) {
        return -EINVAL;
    }

    out_capture->humidity_loop_ma = loop_current_ma;

    if (!is_valid || isnan(loop_current_ma) || isinf(loop_current_ma)) {
        out_capture->humidity_return_pct = 0.0f;
        out_capture->humidity_quality = QUALITY_FAILED;
        return 0;
    }

    out_capture->humidity_quality = humidity_transducer_process(loop_current_ma,
                                                                &out_capture->humidity_return_pct);
    return 0;
}

/* ========================================================================= */
/* Electrical Meter Normalizer (JSY-MK-339)                                  */
/* ========================================================================= */

int sensor_matrix_map_electrical(const energy_reading_t *elec_reading,
                                 thermal_energy_capture_t *out_capture)
{
    if (out_capture == NULL) {
        return -EINVAL;
    }

    if (elec_reading == NULL || !elec_reading->is_valid) {
        out_capture->elec_quality = QUALITY_FAILED;
        out_capture->total_active_power_kw = 0.0f;
        out_capture->power_factor = 0.0f;
        out_capture->total_active_energy_kwh = 0.0;
        memset(out_capture->voltage_v, 0, sizeof(out_capture->voltage_v));
        memset(out_capture->current_a, 0, sizeof(out_capture->current_a));
        return 0;
    }

    out_capture->elec_timestamp_ms = elec_reading->timestamp_us / 1000ULL;
    out_capture->total_active_power_kw = elec_reading->total_active_power_kw;
    out_capture->power_factor = elec_reading->total_power_factor;
    out_capture->total_active_energy_kwh = elec_reading->total_active_energy_kwh;
    out_capture->elec_quality = QUALITY_VALID;

    for (size_t p = 0; p < 3U; p++) {
        out_capture->voltage_v[p] = elec_reading->voltage_v[p];
        out_capture->current_a[p] = elec_reading->current_a[p];
    }

    return 0;
}

/* ========================================================================= */
/* Digital Contact Normalizer                                                */
/* ========================================================================= */

int sensor_matrix_map_contacts(hvac_operation_mode_t mode,
                               const di_snapshot_msg_t *di_snapshot,
                               uint32_t mbsl32di_bits,
                               thermal_energy_capture_t *out_capture)
{
    if (out_capture == NULL) {
        return -EINVAL;
    }

    out_capture->fan_state_mask = 0U;
    out_capture->comp_state_mask = 0U;
    out_capture->fan_quality_mask = 0U;
    out_capture->comp_quality_mask = 0U;

    if (mode == HVAC_MODE_TELEMETRY_ONLY) {
        /*
         * Telemetry-Only Mode: Local optocoupled inputs ALM_IN_1..16 on MCP23S17.
         * Unit u (0..7):
         * - Compressor aux: Pin 2 * u
         * - Fan aux: Pin 2 * u + 1
         */
        if (di_snapshot != NULL) {
            out_capture->contact_timestamp_ms = di_snapshot->timestamp_ms;
            for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
                uint8_t comp_pin = 2U * u;
                uint8_t fan_pin = (2U * u) + 1U;

                if (comp_pin < di_snapshot->channel_count || di_snapshot->channel_count == 0U) {
                    if ((di_snapshot->active_mask & (1ULL << comp_pin)) != 0ULL) {
                        out_capture->comp_state_mask |= (1U << u);
                    }
                    out_capture->comp_quality_mask |= (1U << u);
                }

                if (fan_pin < di_snapshot->channel_count || di_snapshot->channel_count == 0U) {
                    if ((di_snapshot->active_mask & (1ULL << fan_pin)) != 0ULL) {
                        out_capture->fan_state_mask |= (1U << u);
                    }
                    out_capture->fan_quality_mask |= (1U << u);
                }
            }
        } else {
            /* Fallback to raw bitmask */
            for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
                uint8_t comp_pin = 2U * u;
                uint8_t fan_pin = (2U * u) + 1U;

                if ((mbsl32di_bits & (1UL << comp_pin)) != 0UL) {
                    out_capture->comp_state_mask |= (1U << u);
                }
                if ((mbsl32di_bits & (1UL << fan_pin)) != 0UL) {
                    out_capture->fan_state_mask |= (1U << u);
                }
                out_capture->comp_quality_mask |= (1U << u);
                out_capture->fan_quality_mask |= (1U << u);
            }
        }
    } else {
        /*
         * Hybrid / Control Mode: Remote Modbus MBSL32DI auxiliary feedback channels:
         * - Fans 1..8: Bits 16..23 (system channels 42..49)
         * - Compressors 1..8: Bits 24..31 (system channels 50..57)
         */
        if (di_snapshot != NULL) {
            out_capture->contact_timestamp_ms = di_snapshot->timestamp_ms;
            for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
                uint8_t fan_ch = 42U + u;
                uint8_t comp_ch = 50U + u;

                if (fan_ch < di_snapshot->channel_count) {
                    if ((di_snapshot->active_mask & (1ULL << fan_ch)) != 0ULL) {
                        out_capture->fan_state_mask |= (1U << u);
                    }
                    out_capture->fan_quality_mask |= (1U << u);
                } else if ((mbsl32di_bits & (1UL << (16U + u))) != 0UL) {
                    out_capture->fan_state_mask |= (1U << u);
                    out_capture->fan_quality_mask |= (1U << u);
                }

                if (comp_ch < di_snapshot->channel_count) {
                    if ((di_snapshot->active_mask & (1ULL << comp_ch)) != 0ULL) {
                        out_capture->comp_state_mask |= (1U << u);
                    }
                    out_capture->comp_quality_mask |= (1U << u);
                } else if ((mbsl32di_bits & (1UL << (24U + u))) != 0UL) {
                    out_capture->comp_state_mask |= (1U << u);
                    out_capture->comp_quality_mask |= (1U << u);
                }
            }
        } else {
            /* Direct MBSL32DI bit decoding */
            for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
                if ((mbsl32di_bits & (1UL << (16U + u))) != 0UL) {
                    out_capture->fan_state_mask |= (1U << u);
                }
                if ((mbsl32di_bits & (1UL << (24U + u))) != 0UL) {
                    out_capture->comp_state_mask |= (1U << u);
                }
                out_capture->fan_quality_mask |= (1U << u);
                out_capture->comp_quality_mask |= (1U << u);
            }
        }
    }

    return 0;
}

/* ========================================================================= */
/* Consolidated Normalizer Entry Point                                       */
/* ========================================================================= */

int sensor_matrix_map_capture(const thermal_capture_source_t *source,
                              thermal_energy_capture_t *out_capture)
{
    if (source == NULL || out_capture == NULL) {
        return -EINVAL;
    }

    memset(out_capture, 0, sizeof(*out_capture));

    out_capture->generation_id = source->generation_id;
    out_capture->capture_timestamp_ms = source->timestamp_ms;
    out_capture->temp_timestamp_ms = source->temp_timestamp_ms;
    out_capture->config_version = (source->site_cfg != NULL) ? (uint32_t)source->site_cfg->version : 1U;

    /* 1. Map Temperatures */
    if (source->flat_temps != NULL) {
        (void)sensor_matrix_map_temperatures(source->site_cfg,
                                             source->flat_temps,
                                             source->flat_valid_mask,
                                             out_capture);
    }

    /* 2. Map Humidity (priority to explicit loop override, fallback to flat ID 50) */
    float loop_ma = 0.0f;
    bool loop_valid = false;

    if (source->humidity_loop_valid) {
        loop_ma = source->humidity_loop_ma;
        loop_valid = true;
    } else if (source->flat_temps != NULL) {
        uint8_t j9_idx = SENSOR_FLAT_ID_HUMIDITY_J9_CH1 - 1U;
        if ((source->flat_valid_mask & (1ULL << j9_idx)) != 0ULL) {
            loop_ma = source->flat_temps[j9_idx];
            loop_valid = true;
        }
    }
    (void)sensor_matrix_map_humidity(loop_ma, loop_valid, out_capture);

    /* 3. Map Electrical Telemetry */
    (void)sensor_matrix_map_electrical(source->elec_reading, out_capture);

    /* 4. Map Digital Auxiliary Contacts */
    (void)sensor_matrix_map_contacts(source->hvac_mode,
                                     source->di_snapshot,
                                     source->mbsl32di_raw_mask,
                                     out_capture);

    return 0;
}
