/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register Codec for the JSY-MK-339 Three-Phase AC Electrical Energy Meter.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_JSY_MK339_CODEC_H_
#define APP_DOMAIN_SERVICES_JSY_MK339_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Modbus register addresses for JSY-MK-339 */
#define JSY_MK339_REG_CONFIG                 0x0006U
#define JSY_MK339_REG_ELECTRICAL_BASE        0x0048U

/** Register counts for electrical parameter blocks */
#define JSY_MK339_ELECTRICAL_BASIC_REG_COUNT 31U /**< 0x0048..0x0066 (V, I, P, Q, S, PF, active energy) */
#define JSY_MK339_ELECTRICAL_FULL_REG_COUNT  48U /**< 0x0048..0x0077 (includes reactive, apparent energy & Hz) */

/** Baud rate enumeration codes for config register (Table 1, reg 0x0006) */
#define JSY_MK339_BAUD_CODE_4800             5U
#define JSY_MK339_BAUD_CODE_9600             6U
#define JSY_MK339_BAUD_CODE_19200            7U

/**
 * @brief Parse a raw Modbus register block starting at 0x0048 into an energy reading snapshot.
 *
 * Handles 16-bit electrical parameters (scaled by 100 or 10000) and 32-bit accumulated
 * energies (big-endian word ordering, scaled by 100).
 *
 * @param[in]  regs        Pointer to raw 16-bit Modbus registers starting from 0x0048.
 * @param[in]  reg_count   Number of words available in @p regs (must be at least 31).
 * @param[out] out_reading Destination energy reading snapshot.
 *
 * @return 0 on success, -EINVAL on invalid arguments or short register buffer.
 */
int jsy_mk339_parse_electrical_parameters(const uint16_t *regs, size_t reg_count,
                                          energy_reading_t *out_reading);

/**
 * @brief Pack slave address and baud rate into the 16-bit configuration register (0x0006).
 *
 * Register format:
 * - High byte: Modbus unit address (1..255).
 * - Low byte: Baud rate code (5 = 4800, 6 = 9600, 7 = 19200).
 *
 * @param[in]  slave_addr Modbus unit address (1..255).
 * @param[in]  baudrate   Baud rate in bps (4800, 9600, or 19200).
 * @param[out] out_reg    Pointer to store the packed 16-bit register value.
 *
 * @return 0 on success, -EINVAL on unsupported baud rate or zero address or NULL output.
 */
int jsy_mk339_build_config_register(uint8_t slave_addr, uint32_t baudrate, uint16_t *out_reg);

/**
 * @brief Unpack the 16-bit configuration register (0x0006) into address and baud rate.
 *
 * @param[in]  reg_val        Raw 16-bit register value read from 0x0006.
 * @param[out] out_slave_addr Destination for unpacked Modbus unit address.
 * @param[out] out_baudrate   Destination for unpacked baud rate in bps.
 *
 * @return 0 on success, -EINVAL on NULL output or unknown baud rate code.
 */
int jsy_mk339_parse_config_register(uint16_t reg_val, uint8_t *out_slave_addr,
                                    uint32_t *out_baudrate);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_JSY_MK339_CODEC_H_ */
