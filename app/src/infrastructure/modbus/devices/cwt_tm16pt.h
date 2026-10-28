/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Driver & Protocol Parser for CWT-TM-16PT 16-Channel Temperature Transmitter.
 * Integrates directly with Zephyr nanoMODBUS Module (<zephyr/nanomodbus/nanomodbus_z.h>).
 */

#ifndef APP_MODBUS_DEVICES_CWT_TM16PT_H_
#define APP_MODBUS_DEVICES_CWT_TM16PT_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include "interfaces/i_temperature_bank.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register Addresses (Modbus Hex) */
#define CWT_TM16PT_REG_CONFIG           0x0010U
#define CWT_TM16PT_REG_TEMP_SCALED_BASE 0x0020U
#define CWT_TM16PT_REG_TEMP_FLOAT_BASE  0x0040U

#define CWT_TM16PT_CHANNEL_COUNT        16U
#define CWT_TM16PT_SCALED_REG_COUNT     16U
#define CWT_TM16PT_FLOAT_REG_COUNT      32U

/** Probe disconnected sentinel value in scaled mode (3276.7 °C) */
#define CWT_TM16PT_PROBE_OPEN_CODE      0x7FFFU

/**
 * @brief Parse raw scaled 16-bit register buffer (0x0020 - 0x002F) into temperature bank.
 * Pure logic function - 100% unit-testable without hardware.
 */
int cwt_tm16pt_parse_scaled_temperatures(const uint16_t *regs, size_t reg_count, temperature_bank_16ch_t *out_bank);

/**
 * @brief Parse raw IEEE-754 float32 register buffer (0x0040 - 0x005E) into temperature bank.
 * Pure logic function - 100% unit-testable without hardware.
 */
int cwt_tm16pt_parse_float_temperatures(const uint16_t *regs, size_t reg_count, temperature_bank_16ch_t *out_bank);

/**
 * @brief Pack Slave ID, Baudrate, and Parity into a 16-bit config register for 0x0010.
 */
int cwt_tm16pt_build_config_register(uint8_t slave_id, uint32_t baudrate, uint8_t parity, uint16_t *out_reg);

/**
 * @brief Unpack 16-bit config register from 0x0010 into Slave ID, Baudrate, and Parity.
 */
int cwt_tm16pt_parse_config_register(uint16_t reg_val, uint8_t *out_slave_id, uint32_t *out_baudrate, uint8_t *out_parity);

/**
 * @brief Read all 16 temperature channels via Zephyr nanoMODBUS client.
 *
 * @param client Pointer to thread-safe nmbs_z_client_t instance.
 * @param slave_addr Modbus Slave address (1-247).
 * @param out_bank Output temperature bank.
 * @param lock_timeout Mutex lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int cwt_tm16pt_read_temperatures(nmbs_z_client_t *client, uint8_t slave_addr, 
                                temperature_bank_16ch_t *out_bank, k_timeout_t lock_timeout);

/**
 * @brief Remotely configure CWT-TM-16PT communication parameters via register 0x0010.
 *
 * @param client Pointer to thread-safe nmbs_z_client_t instance.
 * @param current_addr Current Modbus Slave address.
 * @param new_addr New Modbus Slave address to program.
 * @param baudrate Desired baudrate.
 * @param parity Desired parity (0: None, 1: Even, 2: Odd).
 * @param lock_timeout Mutex lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int cwt_tm16pt_write_bus_config(nmbs_z_client_t *client, uint8_t current_addr, 
                                uint8_t new_addr, uint32_t baudrate, uint8_t parity, 
                                k_timeout_t lock_timeout);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODBUS_DEVICES_CWT_TM16PT_H_ */
