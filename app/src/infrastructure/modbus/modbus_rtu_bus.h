/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modbus RTU Bus Controller wrapping Zephyr nanoMODBUS Serial Transport (<zephyr/nanomodbus/nanomodbus_z_serial.h>).
 */

#ifndef APP_MODBUS_RTU_BUS_H_
#define APP_MODBUS_RTU_BUS_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include "common/modbus_slave_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context for a managed physical Modbus RTU Bus.
 */
typedef struct {
    nmbs_z_serial_transport_t transport;
    nmbs_z_client_t client;
    nmbs_platform_conf platform_conf;
    const struct device *uart_dev;
    struct gpio_dt_spec de_gpio;
    uint32_t current_baudrate;
    bool is_initialized;
} modbus_rtu_bus_t;

/**
 * @brief Initialize an RS485 Modbus RTU bus with Zephyr nanoMODBUS transport and client.
 *
 * @param bus Pointer to bus context.
 * @param uart_dev UART device instance (e.g. DEVICE_DT_GET(DT_NODELABEL(usart2))).
 * @param de_gpio Optional DE/RE GPIO pin spec (set NULL if full duplex).
 * @param baudrate Serial baudrate (e.g. 9600).
 * @param parity Parity (0: None, 1: Even, 2: Odd).
 * @param stop_bits Stop bits (1 or 2).
 * @return 0 on success, negative errno on failure.
 */
int modbus_rtu_bus_init(modbus_rtu_bus_t *bus, const struct device *uart_dev, 
                        const struct gpio_dt_spec *de_gpio, uint32_t baudrate, 
                        uint8_t parity, uint8_t stop_bits);

/**
 * @brief Reconfigure bus baudrate, parity, and stop bits in runtime without rebooting.
 */
int modbus_rtu_bus_reconfigure(modbus_rtu_bus_t *bus, uint32_t baudrate, 
                               uint8_t parity, uint8_t stop_bits);

/**
 * @brief Get the thread-safe Zephyr nanoMODBUS client instance for this bus.
 */
nmbs_z_client_t *modbus_rtu_bus_get_client(modbus_rtu_bus_t *bus);

/**
 * @brief De-initialize and stop the bus transport.
 */
void modbus_rtu_bus_deinit(modbus_rtu_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODBUS_RTU_BUS_H_ */
