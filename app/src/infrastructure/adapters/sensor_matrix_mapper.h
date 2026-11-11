/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sensor_matrix_mapper.h
 * @brief Normalizer adapter mapping raw hardware telemetry into thermal_energy_capture_t.
 *
 * Ingestion and normalization layer:
 * - Maps temperatures from flat snapshot (IDs 1..59) according to config_thermal_site_t
 * - Processes 4-20 mA return humidity (J9 CH1) via humidity_transducer_process()
 * - Ingests 3-phase AC energy telemetry from JSY-MK-339
 * - Normalizes digital contacts (compressor and fan run states) according to HVAC mode
 *   (ALM_IN_1..16 in HVAC_MODE_TELEMETRY_ONLY or MBSL32DI in HYBRID_FULL/CONTROL_ONLY)
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_SENSOR_MATRIX_MAPPER_H_
#define APP_INFRASTRUCTURE_ADAPTERS_SENSOR_MATRIX_MAPPER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "common/digital_input_types.h"
#include "common/energy_types.h"
#include "common/hvac_types.h"
#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Flat snapshot sensor ID for 4-20 mA Analog J9 CH1 (Return Air Humidity) */
#define SENSOR_FLAT_ID_HUMIDITY_J9_CH1 (50U)

/**
 * @brief Composite telemetry source bundle for capture snapshot generation.
 */
typedef struct {
    uint32_t                    generation_id;       /**< Monotonic sequence identifier */
    uint64_t                    timestamp_ms;        /**< Timestamp of snapshot assembly */

    /* Site Topology */
    const config_thermal_site_t *site_cfg;           /**< Thermal site topology configuration */

    /* Temperatures Flat Bank */
    const float                *flat_temps;          /**< 64-element flat temperatures array */
    uint64_t                    flat_valid_mask;     /**< 64-bit channel validity bitmask */
    uint64_t                    temp_timestamp_ms;   /**< Acquisition timestamp of temperatures */

    /* Dedicated Humidity Loop Override (Optional) */
    float                       humidity_loop_ma;    /**< Direct loop current [mA] if not from flat ID 50 */
    bool                        humidity_loop_valid; /**< True if humidity_loop_ma should be used */

    /* Electrical Power Meter (JSY-MK-339) */
    const energy_reading_t     *elec_reading;        /**< 3-phase electrical measurements */

    /* Digital Contacts & HVAC Mode */
    hvac_operation_mode_t       hvac_mode;           /**< Operational mode determining contact routing */
    const di_snapshot_msg_t    *di_snapshot;         /**< Consolidated DI snapshot */
    uint32_t                    mbsl32di_raw_mask;   /**< Raw 32-bit MBSL32DI / local DI mask fallback */
} thermal_capture_source_t;

/**
 * @brief Map temperature bank readings into thermal_energy_capture_t according to site config.
 *
 * Maps configured thermal pairs (inj/ret), room probes, and outdoor reference probes.
 *
 * @param[in]  site_cfg        Site topology configuration (or NULL for default pilot 6/2/2).
 * @param[in]  flat_temps       64-element flat temperature snapshot.
 * @param[in]  flat_valid_mask  64-bit validity bitmask.
 * @param[out] out_capture     Destination capture snapshot.
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int sensor_matrix_map_temperatures(const config_thermal_site_t *site_cfg,
                                   const float flat_temps[64],
                                   uint64_t flat_valid_mask,
                                   thermal_energy_capture_t *out_capture);

/**
 * @brief Map and process 4-20 mA humidity current loop reading into capture snapshot.
 *
 * @param[in]  loop_current_ma Measured loop current in mA (3.5 .. 21.0 mA).
 * @param[in]  is_valid        True if current reading is electrically valid.
 * @param[out] out_capture     Destination capture snapshot.
 * @return 0 on success, -EINVAL on NULL destination.
 */
int sensor_matrix_map_humidity(float loop_current_ma,
                               bool is_valid,
                               thermal_energy_capture_t *out_capture);

/**
 * @brief Map 3-phase electrical meter reading (JSY-MK-339) into capture snapshot.
 *
 * @param[in]  elec_reading Source energy reading (or NULL for unmapped/failed).
 * @param[out] out_capture  Destination capture snapshot.
 * @return 0 on success, -EINVAL on NULL destination.
 */
int sensor_matrix_map_electrical(const energy_reading_t *elec_reading,
                                 thermal_energy_capture_t *out_capture);

/**
 * @brief Map fan and compressor auxiliary contacts into capture snapshot according to HVAC mode.
 *
 * - In HVAC_MODE_TELEMETRY_ONLY: maps local contacts ALM_IN_1..16 (Unit u: comp=2*u, fan=2*u+1).
 * - In HYBRID_FULL / CONTROL_ONLY: maps remote MBSL32DI contacts (fans=bits 16..23, comp=bits 24..31).
 *
 * @param[in]  mode              Active HVAC operation mode.
 * @param[in]  di_snapshot       DI snapshot message (can be NULL if mbsl32di_bits supplied).
 * @param[in]  mbsl32di_bits     Raw 32-bit register fallback.
 * @param[out] out_capture       Destination capture snapshot.
 * @return 0 on success, -EINVAL on NULL destination.
 */
int sensor_matrix_map_contacts(hvac_operation_mode_t mode,
                               const di_snapshot_msg_t *di_snapshot,
                               uint32_t mbsl32di_bits,
                               thermal_energy_capture_t *out_capture);

/**
 * @brief Normalize a complete multi-source telemetry bundle into a coherent capture snapshot.
 *
 * @param[in]  source      Complete source bundle containing temperatures, humidity, power, contacts.
 * @param[out] out_capture Destination coherent snapshot.
 * @return 0 on success, -EINVAL on NULL parameters.
 */
int sensor_matrix_map_capture(const thermal_capture_source_t *source,
                              thermal_energy_capture_t *out_capture);

/**
 * @brief Build a coherent multi-source capture snapshot from composite inputs.
 *
 * @param[in]  source      Complete source bundle containing temperatures, humidity, power, contacts.
 * @param[out] out_capture Destination coherent snapshot.
 * @return 0 on success, -EINVAL on NULL parameters.
 */
static inline int sensor_matrix_mapper_build_capture(const thermal_capture_source_t *source,
                                                     thermal_energy_capture_t *out_capture)
{
    return sensor_matrix_map_capture(source, out_capture);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_SENSOR_MATRIX_MAPPER_H_ */
