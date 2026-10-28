/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modbus Slave & Bus Configuration Types (Serializable for microconf / EEPROM).
 * Pure C99/C11 - Zero Hardware Dependencies.
 */

#ifndef APP_COMMON_MODBUS_SLAVE_CONFIG_H_
#define APP_COMMON_MODBUS_SLAVE_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MAX_SLAVES_PER_BUS 16
#define MODBUS_MAX_TOTAL_SLAVES   32

/**
 * @brief Logical identification of physical RS485 buses.
 */
typedef enum {
    MODBUS_BUS_RS485_1 = 0,    /**< RS485-1 (USART2 on BrightC V3) */
    MODBUS_BUS_RS485_2,        /**< RS485-2 (USART6 on BrightC V3) */
    MODBUS_BUS_MAX
} modbus_bus_id_t;

/**
 * @brief Known device types supported by the telemetry system.
 */
typedef enum {
    MODBUS_DEV_TYPE_UNKNOWN = 0,
    MODBUS_DEV_TYPE_CWT_TM16PT,       /**< 16-channel PT100 temperature transmitter */
    MODBUS_DEV_TYPE_ENERGY_METER_SDM, /**< Industrial Eastron/Schneider energy meter */
    MODBUS_DEV_TYPE_GPIO_EXPANDER,    /**< Discrete digital inputs and relay outputs */
} modbus_device_type_t;

/**
 * @brief Runtime health status of a managed slave.
 */
typedef enum {
    MODBUS_HEALTH_ONLINE = 0,         /**< Device responds normally */
    MODBUS_HEALTH_DEGRADED,           /**< Occasional CRC/timeout errors */
    MODBUS_HEALTH_OFFLINE_BACKOFF     /**< Repeated timeouts; placed in slow-polling backoff */
} modbus_health_status_t;

/**
 * @brief Serial UART transport parameters for an RS485 bus.
 * Packed structure for direct serialization with microconf.
 */
typedef struct __attribute__((__packed__)) {
    uint32_t baudrate;   /**< e.g. 9600, 19200, 38400, 115200 */
    uint8_t parity;      /**< 0: None, 1: Even, 2: Odd */
    uint8_t stop_bits;   /**< 1 or 2 */
    uint8_t data_bits;   /**< 8 */
} modbus_bus_transport_cfg_t;

/**
 * @brief Slave device configuration descriptor.
 * Packed structure for direct serialization with microconf / EEPROM.
 */
typedef struct __attribute__((__packed__)) {
    uint8_t device_id;         /**< Application unique logical ID */
    uint8_t bus_id;            /**< MODBUS_BUS_RS485_1 or MODBUS_BUS_RS485_2 */
    uint8_t slave_addr;        /**< Modbus Slave Address (1 to 247) */
    uint8_t dev_type;          /**< modbus_device_type_t */
    uint16_t poll_interval_ms; /**< Normal polling interval (e.g. 1000 ms) */
    uint16_t timeout_ms;       /**< Response timeout (e.g. 300 ms) */
    bool is_enabled;           /**< True if actively polled */
} modbus_slave_descriptor_t;

/**
 * @brief Runtime statistics and health tracking for a slave.
 */
typedef struct {
    modbus_health_status_t health;
    uint32_t successful_polls;
    uint32_t timeout_count;
    uint32_t crc_error_count;
    uint32_t consecutive_fails;
    uint64_t last_poll_timestamp_ms;
    uint64_t last_success_timestamp_ms;
} modbus_slave_runtime_state_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_SLAVE_CONFIG_H_ */
