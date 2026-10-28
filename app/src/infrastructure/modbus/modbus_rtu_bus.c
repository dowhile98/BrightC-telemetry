/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "modbus_rtu_bus.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(modbus_rtu_bus, CONFIG_NANOMODBUS_LOG_LEVEL);

int modbus_rtu_bus_init(modbus_rtu_bus_t *bus, const struct device *uart_dev, 
                        const struct gpio_dt_spec *de_gpio, uint32_t baudrate, 
                        uint8_t parity, uint8_t stop_bits)
{
    if (bus == NULL || uart_dev == NULL) {
        return -EINVAL;
    }

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device %s not ready", uart_dev->name);
        return -ENODEV;
    }

    bus->uart_dev = uart_dev;
    bus->current_baudrate = (baudrate > 0) ? baudrate : 9600;
    bus->is_initialized = false;

    nmbs_z_serial_transport_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart_dev = uart_dev;
    cfg.baud_rate = bus->current_baudrate;
    cfg.data_bits = UART_CFG_DATA_BITS_8;

    switch (parity) {
    case 1: cfg.parity = UART_CFG_PARITY_EVEN; break;
    case 2: cfg.parity = UART_CFG_PARITY_ODD; break;
    default: cfg.parity = UART_CFG_PARITY_NONE; break;
    }

    switch (stop_bits) {
    case 2: cfg.stop_bits = UART_CFG_STOP_BITS_2; break;
    default: cfg.stop_bits = UART_CFG_STOP_BITS_1; break;
    }

    if (de_gpio != NULL && de_gpio->port != NULL) {
        bus->de_gpio = *de_gpio;
        cfg.de_gpio = *de_gpio;
        cfg.de_mode = NMBS_Z_SERIAL_DE_GPIO;
        cfg.de_pre_delay_us = 20;
        cfg.de_post_delay_us = 50;
    } else {
        cfg.de_mode = NMBS_Z_SERIAL_DE_AUTO;
    }

    /* Use interrupt-driven RX mode from Zephyr nanoMODBUS */
    cfg.rx_mode = NMBS_Z_SERIAL_RX_IRQ;

    int err = nmbs_z_serial_transport_init(&bus->transport, &cfg, &bus->platform_conf);
    if (err != 0) {
        LOG_ERR("nmbs_z_serial_transport_init failed: %d", err);
        return err;
    }

    err = nmbs_z_client_init(&bus->client, &bus->platform_conf);
    if (err != 0) {
        LOG_ERR("nmbs_z_client_init failed: %d", err);
        nmbs_z_serial_transport_deinit(&bus->transport);
        return err;
    }

    bus->is_initialized = true;
    LOG_INF("Modbus RTU Bus on %s initialized successfully (%u bps)", uart_dev->name, bus->current_baudrate);
    return 0;
}

int modbus_rtu_bus_reconfigure(modbus_rtu_bus_t *bus, uint32_t baudrate, 
                               uint8_t parity, uint8_t stop_bits)
{
    if (bus == NULL || !bus->is_initialized || bus->uart_dev == NULL) {
        return -EINVAL;
    }

    struct uart_config ucfg = {
        .baudrate = baudrate,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    switch (parity) {
    case 1: ucfg.parity = UART_CFG_PARITY_EVEN; break;
    case 2: ucfg.parity = UART_CFG_PARITY_ODD; break;
    default: ucfg.parity = UART_CFG_PARITY_NONE; break;
    }

    switch (stop_bits) {
    case 2: ucfg.stop_bits = UART_CFG_STOP_BITS_2; break;
    default: ucfg.stop_bits = UART_CFG_STOP_BITS_1; break;
    }

    int err = uart_configure(bus->uart_dev, &ucfg);
    if (err == 0) {
        bus->current_baudrate = baudrate;
        /* Recalculate Modbus character silence timings */
        nmbs_z_serial_calc_timings(baudrate, &bus->transport.t1_5_us, &bus->transport.t3_5_us);
        LOG_INF("Modbus RTU Bus reconfigured: %u bps, parity %u", baudrate, parity);
    } else {
        LOG_ERR("uart_configure failed: %d", err);
    }

    return err;
}

nmbs_z_client_t *modbus_rtu_bus_get_client(modbus_rtu_bus_t *bus)
{
    if (bus == NULL || !bus->is_initialized) {
        return NULL;
    }
    return &bus->client;
}

void modbus_rtu_bus_deinit(modbus_rtu_bus_t *bus)
{
    if (bus != NULL && bus->is_initialized) {
        nmbs_z_serial_transport_deinit(&bus->transport);
        bus->is_initialized = false;
    }
}
