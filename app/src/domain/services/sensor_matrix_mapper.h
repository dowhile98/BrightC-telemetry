/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor matrix domain ingestion, calibration and mapping engine.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_
#define APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "common/sensor_types.h"
#include "interfaces/i_temperature_bank.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a default pilot profile channel configuration table.
 *
 * Maps CWT CH1-16 to 6 pairs + 2 room + 2 outdoor probes,
 * disables internal RTD 1-5 with 0 offset/gain,
 * configures J9 CH1 for return humidity.
 *
 * @param[out] configs  Destination array of at least @ref SENSOR_PHYS_CH_MAX entries.
 * @param[in]  max_entries Available capacity in @p configs.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int sensor_matrix_init_default_pilot_config(sensor_channel_cfg_t *configs, size_t max_entries);

/**
 * @brief Map raw peripheral readings into a semantic thermal matrix frame.
 *
 * Consumes raw physical channels (external Modbus CWT, internal RTD/ADC, J9 current loop),
 * evaluates validity and open probe status, converts internal RTD ADC counts using
 * Callendar-Van Dusen equation, applies 2-point calibration offset and gain, routes channels
 * to their configured semantic role, and computes pair consensus masks.
 *
 * @param[in]  raw         Raw sensor readings bank.
 * @param[in]  configs     Channel configuration table.
 * @param[in]  cfg_count   Number of configuration entries in @p configs.
 * @param[out] out_frame   Semantic thermal matrix frame destination.
 *
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int sensor_matrix_map(const raw_sensor_readings_t *raw,
                      const sensor_channel_cfg_t *configs,
                      size_t cfg_count,
                      thermal_matrix_frame_t *out_frame);

/**
 * @brief Pack a semantic frame into the flat 16-slot HVAC temperature bank.
 *
 * HVAC group entities consume temperatures through a flat 16-entry array
 * addressed by `config_hvac_group_t::sensor_mask`. This helper preserves the
 * pilot CWT wiring layout (the same layout produced by
 * sensor_matrix_init_default_pilot_config()) so the semantic layer can feed
 * HVAC without changing the group configuration semantics:
 *
 *   slot 0..5   supply air of pairs 0..5
 *   slot 6..11  return air of pairs 0..5
 *   slot 12     room ambient probe 5
 *   slot 13     room ambient probe 6
 *   slot 14     outdoor ambient probe 0
 *   slot 15     outdoor ambient probe 1
 *
 * Disabled or out-of-range slots are reported as invalid.
 *
 * @param[in]  frame          Semantic frame to flatten.
 * @param[out] out_temps      Destination 16-entry temperature array in degC.
 * @param[out] out_valid_mask Destination validity bitmask (bit s = slot s valid).
 * @return 0 on success, -EINVAL on null pointers.
 */
int sensor_matrix_pack_hvac_bank(const thermal_matrix_frame_t *frame,
                                 float out_temps[TEMPERATURE_BANK_MAX_CHANNELS],
                                 uint16_t *out_valid_mask);

/**
 * @brief Pack raw sensor readings into a unified flat sensor snapshot.
 *
 * Maps physical acquisition channels into flat 1-byte sensor IDs (1..59):
 * - Internal on-board RTDs 1..5 (PHYS_CH_INTERNAL_RTD_1..5) -> flat IDs 1..5 (index 0..4, bit 0..4)
 * - Modbus CWT Concentrator 1 channels 1..16 (PHYS_CH_CWT_1..16) -> flat IDs 10..25 (index 9..24, bit 9..24)
 * - Modbus CWT Concentrator 2 channels 1..16 (PHYS_CH_CWT2_1..16) -> flat IDs 30..45 (index 29..44, bit 29..44)
 * - Analog J9 channels 1..10 (PHYS_CH_ANALOG_J9_CH1..10) -> flat IDs 50..59 (index 49..58, bit 49..58)
 *
 * Bounds checked: all flat IDs satisfy (id - 1 < 64).
 *
 * @param[in]  raw            Raw readings bank from sensor matrix adapter.
 * @param[out] out_temps      Flat 64-element array indexed by (id - 1).
 * @param[out] out_valid_mask 64-bit mask where bit (id - 1) is set if valid.
 * @return 0 on success, -EINVAL on null pointers.
 */
int sensor_matrix_pack_flat_snapshot(const sensor_matrix_raw_bank_t *raw,
                                     float out_temps[64],
                                     uint64_t *out_valid_mask);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_ */
