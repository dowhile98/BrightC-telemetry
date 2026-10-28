/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure register codec for the CWT-TM-16PT 16-channel temperature transmitter.
 * Pure C99/C11 - no Zephyr headers, no transport, no hardware dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_CWT_TM16PT_CODEC_H_
#define APP_DOMAIN_SERVICES_CWT_TM16PT_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_temperature_bank.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register addresses of the CWT-TM-16PT (Modbus hex). */
#define CWT_TM16PT_REG_CONFIG           0x0010U
#define CWT_TM16PT_REG_TEMP_SCALED_BASE 0x0020U
#define CWT_TM16PT_REG_TEMP_FLOAT_BASE  0x0040U

/** Channel and register counts of the device. */
#define CWT_TM16PT_CHANNEL_COUNT        16U
#define CWT_TM16PT_SCALED_REG_COUNT     16U
#define CWT_TM16PT_FLOAT_REG_COUNT      32U

/** Probe disconnected sentinel value in scaled mode (3276.7 C). */
#define CWT_TM16PT_PROBE_OPEN_CODE      0x7FFFU

/**
 * @brief Parse a raw scaled 16-bit register block (0x0020 - 0x002F).
 *
 * @param[in]  regs      Register buffer, at least @ref CWT_TM16PT_SCALED_REG_COUNT words.
 * @param[in]  reg_count Number of words available in @p regs.
 * @param[out] out_bank  Destination temperature bank.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int cwt_tm16pt_parse_scaled_temperatures(const uint16_t *regs, size_t reg_count,
                                         temperature_bank_16ch_t *out_bank);

/**
 * @brief Parse a raw IEEE-754 float32 register block (0x0040 - 0x005E).
 *
 * @param[in]  regs      Register buffer, at least @ref CWT_TM16PT_FLOAT_REG_COUNT words.
 * @param[in]  reg_count Number of words available in @p regs.
 * @param[out] out_bank  Destination temperature bank.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int cwt_tm16pt_parse_float_temperatures(const uint16_t *regs, size_t reg_count,
                                        temperature_bank_16ch_t *out_bank);

/**
 * @brief Pack slave id, baudrate and parity into the 16-bit config register.
 *
 * @param[in]  slave_id Modbus unit identifier to program (non zero).
 * @param[in]  baudrate Line speed supported by the device.
 * @param[in]  parity   Parity code (0 none, 1 even, 2 odd).
 * @param[out] out_reg  Packed register value.
 *
 * @return 0 on success, -EINVAL on an unsupported value.
 */
int cwt_tm16pt_build_config_register(uint8_t slave_id, uint32_t baudrate, uint8_t parity,
                                     uint16_t *out_reg);

/**
 * @brief Unpack the 16-bit config register into its three fields.
 *
 * @param[in]  reg_val      Raw register value read from the device.
 * @param[out] out_slave_id Modbus unit identifier.
 * @param[out] out_baudrate Decoded line speed.
 * @param[out] out_parity   Decoded parity code (0 none, 1 even, 2 odd).
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int cwt_tm16pt_parse_config_register(uint16_t reg_val, uint8_t *out_slave_id,
                                     uint32_t *out_baudrate, uint8_t *out_parity);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_CWT_TM16PT_CODEC_H_ */
