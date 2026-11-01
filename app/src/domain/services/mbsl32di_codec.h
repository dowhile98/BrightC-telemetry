/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register Codec for the MBSL32DI 32-Channel Digital Input Module.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_MBSL32DI_CODEC_H_
#define APP_DOMAIN_SERVICES_MBSL32DI_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Modbus register addresses for MBSL32DI */
#define MBSL32DI_REG_INPUTS_BASE         0x0000U
#define MBSL32DI_REG_CONFIG_BASE         0x0002U

/** Register counts */
#define MBSL32DI_INPUTS_REG_COUNT        2U
#define MBSL32DI_CONFIG_REG_COUNT        5U
#define MBSL32DI_CONFIG_WRITE_REG_COUNT  4U

/** Channel counts and limits */
#define MBSL32DI_CHANNEL_MIN             1U
#define MBSL32DI_CHANNEL_MAX             32U

/** Baud rate enumeration codes for config register (Reg 0x0003) */
#define MBSL32DI_BAUD_CODE_4800          1U
#define MBSL32DI_BAUD_CODE_9600          2U
#define MBSL32DI_BAUD_CODE_19200         3U
#define MBSL32DI_BAUD_CODE_38400         4U
#define MBSL32DI_BAUD_CODE_57600         5U
#define MBSL32DI_BAUD_CODE_115200        6U

/** Parity enumeration codes for config register (Reg 0x0004) */
#define MBSL32DI_PARITY_NONE             1U
#define MBSL32DI_PARITY_ODD              2U
#define MBSL32DI_PARITY_EVEN             3U

/** Disconnection timeout sentinel value (>= 1800 disables timeout) */
#define MBSL32DI_TIMEOUT_DISABLE_THRESHOLD 1800U

/**
 * @brief MBSL32DI digital input snapshot reading.
 */
typedef struct {
    uint32_t raw_inputs; /**< Bit 0 = Ch 1 (I00) .. Bit 31 = Ch 32 (I37). 1 = active, 0 = open. */
    bool is_valid;       /**< True if communication and data are valid */
} mbsl32di_reading_t;

/**
 * @brief MBSL32DI configuration structure.
 */
typedef struct {
    uint8_t slave_addr;  /**< Modbus unit identifier (1..255) */
    uint32_t baudrate;   /**< Line baud rate in bps (4800, 9600, 19200, 38400, 57600, 115200) */
    uint8_t parity;      /**< Parity code (1 = None, 2 = Odd, 3 = Even) */
    uint16_t timeout_s;  /**< Disconnection timeout in seconds (>=1800 disables timeout) */
    uint16_t version;    /**< Firmware / Software Version */
} mbsl32di_config_t;

/**
 * @brief Query the active state of a specific digital input channel (1..32).
 *
 * @param[in] reading          Pointer to parsed input reading.
 * @param[in] channel_1_to_32  Channel number from 1 to 32 (1 corresponds to I00, 32 to I37).
 *
 * @return true if channel is active/closed, false if open, invalid, or parameters out of range.
 */
bool mbsl32di_get_channel(const mbsl32di_reading_t *reading, uint8_t channel_1_to_32);

/**
 * @brief Parse raw Modbus holding registers (0x0000..0x0001) into digital input reading.
 *
 * Register 0x0000: Channels 1..16 (Bit 0 = Ch 1 / I00, Bit 15 = Ch 16 / I17).
 * Register 0x0001: Channels 17..32 (Bit 0 = Ch 17 / I20, Bit 15 = Ch 32 / I37).
 *
 * @param[in]  regs        Pointer to register array starting at 0x0000.
 * @param[in]  reg_count   Number of registers available (must be at least 2).
 * @param[out] out_reading Destination reading structure.
 *
 * @return 0 on success, -EINVAL on invalid arguments or insufficient register count.
 */
int mbsl32di_parse_inputs(const uint16_t *regs, size_t reg_count, mbsl32di_reading_t *out_reading);

/**
 * @brief Parse raw Modbus holding registers starting at 0x0002 into configuration structure.
 *
 * Registers:
 * - 0x0002: Slave Address (1..255)
 * - 0x0003: Baud Rate Code (1=4800, 2=9600, 3=19200, 4=38400, 5=57600, 6=115200)
 * - 0x0004: Parity Code (1=None, 2=Odd, 3=Even)
 * - 0x0005: Disconnection timeout in seconds
 * - 0x0006: Firmware / Software Version (optional if reg_count == 4)
 *
 * @param[in]  regs      Pointer to register array starting at 0x0002.
 * @param[in]  reg_count Number of registers available (must be at least 4).
 * @param[out] out_cfg   Destination configuration structure.
 *
 * @return 0 on success, -EINVAL on invalid arguments, unknown codes, or short buffer.
 */
int mbsl32di_parse_config(const uint16_t *regs, size_t reg_count, mbsl32di_config_t *out_cfg);

/**
 * @brief Build raw Modbus registers (0x0002..0x0005) for writing module configuration.
 *
 * @param[in]  slave_addr Modbus slave address (1..255).
 * @param[in]  baudrate   Baud rate in bps (4800, 9600, 19200, 38400, 57600, 115200).
 * @param[in]  parity     Parity code (1=None, 2=Odd, 3=Even).
 * @param[in]  timeout_s  Disconnection timeout in seconds.
 * @param[out] out_regs   Destination register buffer (at least 4 words).
 * @param[in]  out_count  Capacity of @p out_regs (must be at least 4).
 *
 * @return 0 on success, -EINVAL on unsupported parameters or buffer too small.
 */
int mbsl32di_build_config_registers(uint8_t slave_addr, uint32_t baudrate, uint8_t parity,
                                    uint16_t timeout_s, uint16_t *out_regs, size_t out_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_MBSL32DI_CODEC_H_ */
