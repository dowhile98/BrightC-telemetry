/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor subsystem common types, channel descriptors and semantic matrices.
 * Pure C99/C11 - Zero RTOS / Zero Hardware Dependencies.
 */

#ifndef APP_COMMON_SENSOR_TYPES_H_
#define APP_COMMON_SENSOR_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum physical endpoints supported across hardware buses */
#define SENSOR_PHYS_CH_MAX 79U

/** Maximum paired supply/return couples supported (up to 16 HVAC units) */
#define SENSOR_MATRIX_MAX_PAIRS 16U         /**< Max supply/return pairs (up to 16 HVAC units) */

/** Maximum room ambient monitoring probes supported (up to 16 probes) */
#define SENSOR_MATRIX_MAX_ROOM_PROBES 16U   /**< Max room ambient probes (up to 16 probes) */

/** Maximum outdoor ambient condensing probes supported (up to 8 probes) */
#define SENSOR_MATRIX_MAX_OUTDOOR_PROBES 8U /**< Max outdoor condenser probes (up to 8 probes) */

/** Multi-concentrator Modbus constants */
#define SENSOR_MAX_CWT_CONCENTRATORS 4U
#define SENSOR_CWT_CHANNELS_PER_DEV 16U
#define SENSOR_MAX_CWT_CHANNELS (SENSOR_MAX_CWT_CONCENTRATORS * SENSOR_CWT_CHANNELS_PER_DEV) /**< 64 Modbus channels */

/** Temperature physical sanity bounds in degrees Celsius per Revision F */
#define SENSOR_TEMP_MIN_VALID_C (-20.0f)
#define SENSOR_TEMP_MAX_VALID_C (80.0f)

/** 4-20 mA current loop validity bounds per NAMUR NE43 standard */
#define SENSOR_4_20MA_LOOP_MIN_MA (3.5f)
#define SENSOR_4_20MA_LOOP_MAX_MA (21.0f)

/** Calibration gain unity scaling factor (10000 counts = 1.0 = 1,000,000 ppm) */
#define SENSOR_CAL_GAIN_UNITY 10000U

/**
 * @brief Fixed hardware acquisition endpoint identifiers.
 */
typedef enum {
    /* External RS-485 Modbus RTU Concentrator 1: CWT-TM-16PT (Channels 1 to 16) */
    PHYS_CH_CWT_1 = 0,
    PHYS_CH_CWT_2,
    PHYS_CH_CWT_3,
    PHYS_CH_CWT_4,
    PHYS_CH_CWT_5,
    PHYS_CH_CWT_6,
    PHYS_CH_CWT_7,
    PHYS_CH_CWT_8,
    PHYS_CH_CWT_9,
    PHYS_CH_CWT_10,
    PHYS_CH_CWT_11,
    PHYS_CH_CWT_12,
    PHYS_CH_CWT_13,
    PHYS_CH_CWT_14,
    PHYS_CH_CWT_15,
    PHYS_CH_CWT_16,

    /* Internal On-Board RTD Stage with INA826 (Channels 1 to 5) */
    PHYS_CH_INTERNAL_RTD_1 = 16,
    PHYS_CH_INTERNAL_RTD_2,
    PHYS_CH_INTERNAL_RTD_3,
    PHYS_CH_INTERNAL_RTD_4,
    PHYS_CH_INTERNAL_RTD_5,

    /* Analog 4-20 mA Inputs on Connector J9 (Channels 1 to 10) */
    PHYS_CH_ANALOG_J9_CH1 = 21,
    PHYS_CH_ANALOG_J9_CH2,
    PHYS_CH_ANALOG_J9_CH3,
    PHYS_CH_ANALOG_J9_CH4,
    PHYS_CH_ANALOG_J9_CH5,
    PHYS_CH_ANALOG_J9_CH6,
    PHYS_CH_ANALOG_J9_CH7,
    PHYS_CH_ANALOG_J9_CH8,
    PHYS_CH_ANALOG_J9_CH9,
    PHYS_CH_ANALOG_J9_CH10,

    /* Extended External RS-485 Modbus RTU Concentrators 2..4 */
    /* Concentrator 2 (channels 1..16) */
    PHYS_CH_CWT2_1 = 31,
    PHYS_CH_CWT2_2,
    PHYS_CH_CWT2_3,
    PHYS_CH_CWT2_4,
    PHYS_CH_CWT2_5,
    PHYS_CH_CWT2_6,
    PHYS_CH_CWT2_7,
    PHYS_CH_CWT2_8,
    PHYS_CH_CWT2_9,
    PHYS_CH_CWT2_10,
    PHYS_CH_CWT2_11,
    PHYS_CH_CWT2_12,
    PHYS_CH_CWT2_13,
    PHYS_CH_CWT2_14,
    PHYS_CH_CWT2_15,
    PHYS_CH_CWT2_16,

    /* Concentrator 3 (channels 1..16) */
    PHYS_CH_CWT3_1 = 47,
    PHYS_CH_CWT3_2,
    PHYS_CH_CWT3_3,
    PHYS_CH_CWT3_4,
    PHYS_CH_CWT3_5,
    PHYS_CH_CWT3_6,
    PHYS_CH_CWT3_7,
    PHYS_CH_CWT3_8,
    PHYS_CH_CWT3_9,
    PHYS_CH_CWT3_10,
    PHYS_CH_CWT3_11,
    PHYS_CH_CWT3_12,
    PHYS_CH_CWT3_13,
    PHYS_CH_CWT3_14,
    PHYS_CH_CWT3_15,
    PHYS_CH_CWT3_16,

    /* Concentrator 4 (channels 1..16) */
    PHYS_CH_CWT4_1 = 63,
    PHYS_CH_CWT4_2,
    PHYS_CH_CWT4_3,
    PHYS_CH_CWT4_4,
    PHYS_CH_CWT4_5,
    PHYS_CH_CWT4_6,
    PHYS_CH_CWT4_7,
    PHYS_CH_CWT4_8,
    PHYS_CH_CWT4_9,
    PHYS_CH_CWT4_10,
    PHYS_CH_CWT4_11,
    PHYS_CH_CWT4_12,
    PHYS_CH_CWT4_13,
    PHYS_CH_CWT4_14,
    PHYS_CH_CWT4_15,
    PHYS_CH_CWT4_16,

    PHYS_CH_MAX = 79
} physical_channel_id_t;

/**
 * @brief Validate physical channel identifier.
 */
static inline bool physical_channel_is_valid(physical_channel_id_t ch)
{
    return (unsigned int)ch < (unsigned int)PHYS_CH_MAX;
}

/**
 * @brief Determine if a physical channel is a CWT Modbus concentrator channel,
 *        and extract the 0-based concentrator index and channel index (0..15).
 *
 * @param[in]  ch               Physical channel ID.
 * @param[out] out_concentrator Optional pointer to receive concentrator index (0..3).
 * @param[out] out_channel      Optional pointer to receive channel index (0..15).
 * @return true if channel is a CWT concentrator channel, false otherwise.
 */
static inline bool physical_channel_is_cwt(physical_channel_id_t ch,
                                           uint8_t *out_concentrator,
                                           uint8_t *out_channel)
{
    uint8_t conc = 0;
    uint8_t dev_ch = 0;

    if (ch >= PHYS_CH_CWT_1 && ch <= PHYS_CH_CWT_16) {
        conc = 0;
        dev_ch = (uint8_t)(ch - PHYS_CH_CWT_1);
    } else if (ch >= PHYS_CH_CWT2_1 && ch <= PHYS_CH_CWT2_16) {
        conc = 1;
        dev_ch = (uint8_t)(ch - PHYS_CH_CWT2_1);
    } else if (ch >= PHYS_CH_CWT3_1 && ch <= PHYS_CH_CWT3_16) {
        conc = 2;
        dev_ch = (uint8_t)(ch - PHYS_CH_CWT3_1);
    } else if (ch >= PHYS_CH_CWT4_1 && ch <= PHYS_CH_CWT4_16) {
        conc = 3;
        dev_ch = (uint8_t)(ch - PHYS_CH_CWT4_1);
    } else {
        return false;
    }

    if (out_concentrator != NULL) {
        *out_concentrator = conc;
    }
    if (out_channel != NULL) {
        *out_channel = dev_ch;
    }
    return true;
}

/**
 * @brief Semantic engineering role assigned to a physical acquisition point.
 */
typedef enum {
    SENSOR_ROLE_DISABLED = 0,         /**< Channel unassigned / inactive */

    /* Temperature Roles (Revision F: (N+N) + M + L) */
    SENSOR_ROLE_TEMP_SUPPLY_AIR,      /**< Cold air supply probe (target_index = pair 0..N-1) */
    SENSOR_ROLE_TEMP_RETURN_AIR,      /**< Warm air return probe (target_index = pair 0..N-1) */
    SENSOR_ROLE_TEMP_ROOM_AMBIENT,    /**< Room zone ambient probe (target_index = probe 0..M-1) */
    SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, /**< Outdoor condenser reference (target_index = probe 0..L-1) */

    /* Psychrometric & Pressure Roles */
    SENSOR_ROLE_HUMIDITY_RETURN,      /**< 4-20 mA Return grille relative humidity (HR_ret) */
    SENSOR_ROLE_PRESSURE_REFRIGERANT, /**< Phase 2: Refrigerant pressure 0-45 bar */
    SENSOR_ROLE_PRESSURE_AIR_FILTER,  /**< Phase 2: Filter differential pressure 0-500 Pa */

    SENSOR_ROLE_MAX
} sensor_role_t;

/**
 * @brief Validate sensor role.
 */
static inline bool sensor_role_is_valid(sensor_role_t role)
{
    return (unsigned int)role < (unsigned int)SENSOR_ROLE_MAX;
}

/**
 * @brief Channel Mapping Descriptor (flat, packed, 8 bytes, EEPROM-serializable).
 */
typedef struct __attribute__((packed)) {
    uint8_t  physical_ch;             /**< physical_channel_id_t */
    uint8_t  role;                    /**< sensor_role_t */
    uint8_t  target_index;            /**< Pair index k (0..N-1) or Probe index m/l */
    int16_t  cal_offset_c_centi;      /**< 2-point calibration offset in 0.01 deg C (int16_t) */
    uint16_t cal_gain_ppm;            /**< 2-point calibration gain scale (0 or 10000 = 1.0) */
    uint8_t  associated_hvac_unit;    /**< HVAC unit index u (1..3) for mass flow continuity */
} sensor_channel_cfg_t;

/**
 * @brief Helper to convert calibration offset in centi-degC to float degC.
 */
static inline float sensor_cal_offset_to_c(int16_t offset_centi)
{
    return (float)offset_centi / 100.0f;
}

/**
 * @brief Helper to convert float degC offset to centi-degC integer.
 */
static inline int16_t sensor_cal_offset_from_c(float offset_c)
{
    return (int16_t)(offset_c * 100.0f + (offset_c >= 0.0f ? 0.5f : -0.5f));
}

/**
 * @brief Helper to convert stored calibration gain (0 or 10000 = 1.0) to float multiplier.
 */
static inline float sensor_cal_gain_to_factor(uint16_t gain_val)
{
    if (gain_val == 0U || gain_val == SENSOR_CAL_GAIN_UNITY) {
        return 1.0f;
    }
    return (float)gain_val / (float)SENSOR_CAL_GAIN_UNITY;
}

/**
 * @brief Helper to convert float multiplier to stored calibration gain.
 */
static inline uint16_t sensor_cal_gain_from_factor(float factor)
{
    if (factor <= 0.0f) {
        return SENSOR_CAL_GAIN_UNITY;
    }
    return (uint16_t)(factor * (float)SENSOR_CAL_GAIN_UNITY + 0.5f);
}

/**
 * @brief Raw readings bank gathered from physical peripheral adapters.
 */
typedef struct {
    uint64_t timestamp_us;

    /* External Modbus CWT-TM-16PT readings (up to 4 concentrators x 16 channels) */
    float    cwt_temperatures_c[SENSOR_MAX_CWT_CONCENTRATORS][16];
    uint16_t cwt_valid_mask[SENSOR_MAX_CWT_CONCENTRATORS];          /**< Bit k = 1 if channel reading is valid */
    uint16_t cwt_open_probe_mask[SENSOR_MAX_CWT_CONCENTRATORS];     /**< Bit k = 1 if open/disconnected */
    bool     cwt_comm_ok[SENSOR_MAX_CWT_CONCENTRATORS];             /**< True if Modbus query succeeded */

    /* Internal Placa V3 RTD channels (Channels 0..4 -> PHYS_CH_INTERNAL_RTD_1..5) */
    float    internal_rtd_raw_adc[5]; /**< Raw ADC counts (0.0 to 65535.0) */
    uint8_t  internal_rtd_valid_mask; /**< Bit k = 1 if ADC reading is valid */

    /* Analog 4-20 mA Inputs on Connector J9 (Channels 0..9 -> PHYS_CH_ANALOG_J9_CH1..10) */
    float    analog_j9_raw_adc[10];   /**< Raw ADC counts (0.0 to 65535.0) */
    uint16_t analog_j9_valid_mask;    /**< Bit k = 1 if ADC reading is valid */
} raw_sensor_readings_t;

/** Alias for raw sensor readings bank in the sensor matrix domain. */
typedef raw_sensor_readings_t sensor_matrix_raw_bank_t;

/**
 * @brief Hardware-agnostic Semantic Ingestion Matrix delivered to the thermodynamic engine.
 */
typedef struct {
    uint64_t timestamp_us;

    /* Paired Evaporator Temperatures */
    uint8_t  configured_pairs;        /**< N (e.g., 6 in pilot, up to 16) */
    float    t_inj[SENSOR_MATRIX_MAX_PAIRS];
    float    t_ret[SENSOR_MATRIX_MAX_PAIRS];
    uint8_t  pair_hvac_unit_map[SENSOR_MATRIX_MAX_PAIRS]; /**< HVAC Unit ID owning this pair */
    uint16_t pair_valid_mask;         /**< Bit k = 1 if BOTH t_inj and t_ret are valid and healthy */
    uint16_t pair_open_probe_mask;    /**< Bit k = 1 if either probe is disconnected */
    uint16_t inj_valid_mask;          /**< Bit k = 1 if t_inj[k] is valid and not open */
    uint16_t ret_valid_mask;          /**< Bit k = 1 if t_ret[k] is valid and not open */

    /* Room Ambient Probes */
    uint8_t  configured_room_probes;  /**< M (e.g., 2 in pilot, up to 16) */
    float    t_room[SENSOR_MATRIX_MAX_ROOM_PROBES];
    uint16_t room_valid_mask;         /**< Bit m = 1 if probe healthy (uint16_t for 16 probes) */

    /* Outdoor Condenser Probes */
    uint8_t  configured_outdoor_probes;/**< L (e.g., 2 in pilot, up to 8) */
    float    t_outdoor[SENSOR_MATRIX_MAX_OUTDOOR_PROBES];
    uint8_t  outdoor_valid_mask;      /**< Bit l = 1 if probe healthy */

    /* Psychrometric & Pressure Channels */
    float    humidity_return_pct;     /**< HR_ret in % (0.0 to 100.0) */
    bool     humidity_return_valid;   /**< True if 4-20 mA loop current is within 3.5 - 21.0 mA */

    float    refrigerant_pressure_bar;/**< Phase 2: R410A gauge pressure (0 to 45 bar) */
    bool     refrigerant_pressure_valid;

    float    filter_diff_pressure_pa; /**< Phase 2: Air filter DP (0 to 500 Pa) */
    bool     filter_diff_pressure_valid;
} thermal_matrix_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_SENSOR_TYPES_H_ */
