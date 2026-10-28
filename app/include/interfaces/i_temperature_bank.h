/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * 16-Channel Temperature Bank Data Contract.
 * Pure C99/C11 - Zero Hardware Dependencies.
 */

#ifndef APP_INTERFACES_I_TEMPERATURE_BANK_H_
#define APP_INTERFACES_I_TEMPERATURE_BANK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEMPERATURE_BANK_MAX_CHANNELS 16

/**
 * @brief 16-channel temperature bank reading.
 */
typedef struct {
    uint8_t device_id;                                  /**< Logical ID of the reporting sensor */
    uint64_t timestamp_us;                              /**< Acquisition monotonic timestamp (us) */
    float temperatures_c[TEMPERATURE_BANK_MAX_CHANNELS];/**< Temperature in degrees Celsius per channel */
    uint16_t valid_channels_mask;                       /**< Bitmask: 1 = channel valid, 0 = probe open/error */
    uint16_t open_probe_mask;                           /**< Bitmask: 1 = sensor disconnected / open circuit */
    bool is_communication_valid;                        /**< True if Modbus transaction succeeded */
} temperature_bank_16ch_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_TEMPERATURE_BANK_H_ */
