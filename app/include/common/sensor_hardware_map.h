/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sensor_hardware_map.h
 * @brief Declarative compile-time physical-to-semantic sensor catalog for BrightC V3.
 *
 * Provides a flat, deterministic 1-byte sensor ID mapping for:
 * - Internal RTDs (1..5)
 * - External Modbus CWT Concentrator 1 (10..25)
 * - External Modbus CWT Concentrator 2 (30..45)
 * - Analog 4-20 mA J9 channels (50..59)
 *
 * Adheres to Clean Architecture (§4.1.D): pure ISO C99/C11 domain declarations,
 * zero Zephyr headers, zero dynamic memory allocation.
 */

#ifndef APP_COMMON_SENSOR_HARDWARE_MAP_H_
#define APP_COMMON_SENSOR_HARDWARE_MAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Flat Sensor Identifier Allocation Ranges                                  */
/* ========================================================================= */

#define SENSOR_ID_INTERNAL_RTD_START     1U
#define SENSOR_ID_INTERNAL_RTD_END       5U
#define SENSOR_ID_INTERNAL_RTD_COUNT     5U

#define SENSOR_ID_MODBUS_CWT1_START      10U
#define SENSOR_ID_MODBUS_CWT1_END        25U
#define SENSOR_ID_MODBUS_CWT1_COUNT      16U

#define SENSOR_ID_MODBUS_CWT2_START      30U
#define SENSOR_ID_MODBUS_CWT2_END        45U
#define SENSOR_ID_MODBUS_CWT2_COUNT      16U

#define SENSOR_ID_ANALOG_J9_START        50U
#define SENSOR_ID_ANALOG_J9_END          59U
#define SENSOR_ID_ANALOG_J9_COUNT        10U

/* Specialized Functional Sensor IDs */
#define SENSOR_ID_ROOM_HUMIDITY_J9_1     50U /**< J9 Ch1: 4-20 mA Return/Room Relative Humidity */
#define SENSOR_ID_FILTER_DP_J9_2         51U /**< J9 Ch2: 4-20 mA Air Filter Differential Pressure */

/**
 * @brief Sensor channel descriptor linking a flat ID to physical endpoints and semantic roles.
 */
typedef struct {
    uint8_t               flat_id;              /**< Flat ID (1..59) */
    physical_channel_id_t physical_ch;          /**< Physical acquisition channel */
    sensor_role_t         default_role;         /**< Semantic role (Supply, Return, Room, Outdoor, etc.) */
    uint8_t               target_index;         /**< Target index (Unit 0..7 or Probe 0..7) */
    uint8_t               associated_hvac_unit; /**< Associated HVAC unit index (1..8, or 0 if none) */
} sensor_hardware_descriptor_t;

/* ========================================================================= */
/* Immutable Standard Sensor Topology Catalog                                */
/* ========================================================================= */

/** Total standard registered descriptors in the primary catalog. */
#define SENSOR_CATALOG_STANDARD_COUNT 33U

static const sensor_hardware_descriptor_t k_brightc_sensor_catalog[SENSOR_CATALOG_STANDARD_COUNT] = {
    /* --- 1. Internal On-Board RTDs (Placa V3, IDs 1..5) --- */
    { .flat_id = 1U,  .physical_ch = PHYS_CH_INTERNAL_RTD_1, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT, .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 2U,  .physical_ch = PHYS_CH_INTERNAL_RTD_2, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT, .target_index = 1U, .associated_hvac_unit = 0U },
    { .flat_id = 3U,  .physical_ch = PHYS_CH_INTERNAL_RTD_3, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT, .target_index = 2U, .associated_hvac_unit = 0U },
    { .flat_id = 4U,  .physical_ch = PHYS_CH_INTERNAL_RTD_4, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT, .target_index = 3U, .associated_hvac_unit = 0U },
    { .flat_id = 5U,  .physical_ch = PHYS_CH_INTERNAL_RTD_5, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT, .target_index = 4U, .associated_hvac_unit = 0U },

    /* --- 2. Modbus Concentrator 1 (CWT-TM-16PT, IDs 10..25) --- */
    /* AC Unit 1 Supply & Return */
    { .flat_id = 10U, .physical_ch = PHYS_CH_CWT_1,  .default_role = SENSOR_ROLE_TEMP_SUPPLY_AIR,      .target_index = 0U, .associated_hvac_unit = 1U },
    { .flat_id = 11U, .physical_ch = PHYS_CH_CWT_2,  .default_role = SENSOR_ROLE_TEMP_RETURN_AIR,      .target_index = 0U, .associated_hvac_unit = 1U },

    /* AC Unit 2 Supply & Return */
    { .flat_id = 12U, .physical_ch = PHYS_CH_CWT_3,  .default_role = SENSOR_ROLE_TEMP_SUPPLY_AIR,      .target_index = 1U, .associated_hvac_unit = 2U },
    { .flat_id = 13U, .physical_ch = PHYS_CH_CWT_4,  .default_role = SENSOR_ROLE_TEMP_RETURN_AIR,      .target_index = 1U, .associated_hvac_unit = 2U },

    /* AC Unit 3 Supply & Return */
    { .flat_id = 14U, .physical_ch = PHYS_CH_CWT_5,  .default_role = SENSOR_ROLE_TEMP_SUPPLY_AIR,      .target_index = 2U, .associated_hvac_unit = 3U },
    { .flat_id = 15U, .physical_ch = PHYS_CH_CWT_6,  .default_role = SENSOR_ROLE_TEMP_RETURN_AIR,      .target_index = 2U, .associated_hvac_unit = 3U },

    /* AC Unit 4 Supply & Return */
    { .flat_id = 16U, .physical_ch = PHYS_CH_CWT_7,  .default_role = SENSOR_ROLE_TEMP_SUPPLY_AIR,      .target_index = 3U, .associated_hvac_unit = 4U },
    { .flat_id = 17U, .physical_ch = PHYS_CH_CWT_8,  .default_role = SENSOR_ROLE_TEMP_RETURN_AIR,      .target_index = 3U, .associated_hvac_unit = 4U },

    /* Room Ambient Probes (Zone Cold/Hot Aisles) */
    { .flat_id = 18U, .physical_ch = PHYS_CH_CWT_9,  .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT,    .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 19U, .physical_ch = PHYS_CH_CWT_10, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT,    .target_index = 1U, .associated_hvac_unit = 0U },
    { .flat_id = 20U, .physical_ch = PHYS_CH_CWT_11, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT,    .target_index = 2U, .associated_hvac_unit = 0U },
    { .flat_id = 21U, .physical_ch = PHYS_CH_CWT_12, .default_role = SENSOR_ROLE_TEMP_ROOM_AMBIENT,    .target_index = 3U, .associated_hvac_unit = 0U },

    /* Outdoor Condenser Probes */
    { .flat_id = 22U, .physical_ch = PHYS_CH_CWT_13, .default_role = SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 23U, .physical_ch = PHYS_CH_CWT_14, .default_role = SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, .target_index = 1U, .associated_hvac_unit = 0U },

    /* Auxiliary / Expansion */
    { .flat_id = 24U, .physical_ch = PHYS_CH_CWT_15, .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 25U, .physical_ch = PHYS_CH_CWT_16, .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },

    /* --- 3. Analog 4-20 mA Inputs (Connector J9, IDs 50..59) --- */
    { .flat_id = 50U, .physical_ch = PHYS_CH_ANALOG_J9_CH1,  .default_role = SENSOR_ROLE_HUMIDITY_RETURN,      .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 51U, .physical_ch = PHYS_CH_ANALOG_J9_CH2,  .default_role = SENSOR_ROLE_PRESSURE_AIR_FILTER,  .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 52U, .physical_ch = PHYS_CH_ANALOG_J9_CH3,  .default_role = SENSOR_ROLE_PRESSURE_REFRIGERANT, .target_index = 0U, .associated_hvac_unit = 1U },
    { .flat_id = 53U, .physical_ch = PHYS_CH_ANALOG_J9_CH4,  .default_role = SENSOR_ROLE_PRESSURE_REFRIGERANT, .target_index = 1U, .associated_hvac_unit = 2U },
    { .flat_id = 54U, .physical_ch = PHYS_CH_ANALOG_J9_CH5,  .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 55U, .physical_ch = PHYS_CH_ANALOG_J9_CH6,  .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 56U, .physical_ch = PHYS_CH_ANALOG_J9_CH7,  .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 57U, .physical_ch = PHYS_CH_ANALOG_J9_CH8,  .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 58U, .physical_ch = PHYS_CH_ANALOG_J9_CH9,  .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
    { .flat_id = 59U, .physical_ch = PHYS_CH_ANALOG_J9_CH10, .default_role = SENSOR_ROLE_DISABLED,             .target_index = 0U, .associated_hvac_unit = 0U },
};

/* ========================================================================= */
/* Helper Queries                                                            */
/* ========================================================================= */

/**
 * @brief Find sensor descriptor by flat ID.
 *
 * @param[in] flat_id Flat sensor ID (1..59).
 * @return Pointer to descriptor, or NULL if not found.
 */
static inline const sensor_hardware_descriptor_t *sensor_hardware_map_get(uint8_t flat_id)
{
    for (size_t i = 0U; i < SENSOR_CATALOG_STANDARD_COUNT; i++) {
        if (k_brightc_sensor_catalog[i].flat_id == flat_id) {
            return &k_brightc_sensor_catalog[i];
        }
    }
    return NULL;
}

/**
 * @brief Check if a flat sensor ID is valid and registered.
 *
 * @param[in] flat_id Flat sensor ID to check.
 * @return true if registered, false otherwise.
 */
static inline bool sensor_hardware_map_is_valid_id(uint8_t flat_id)
{
    return (sensor_hardware_map_get(flat_id) != NULL);
}

/**
 * @brief Retrieve entire immutable sensor catalog.
 *
 * @param[out] out_count Optional pointer receiving total count.
 * @return Pointer to immutable catalog array.
 */
static inline const sensor_hardware_descriptor_t *sensor_hardware_map_get_all(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = SENSOR_CATALOG_STANDARD_COUNT;
    }
    return k_brightc_sensor_catalog;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_SENSOR_HARDWARE_MAP_H_ */
