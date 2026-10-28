/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adapter_modbus_nmbs.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_modbus_nmbs, LOG_LEVEL_INF);

/** Translate a nanoMODBUS error into the application transaction outcome. */
static modbus_transaction_result_t classify(nmbs_error err)
{
    if (err == NMBS_ERROR_NONE) {
        return MODBUS_RESULT_OK;
    }

    if (nmbs_error_is_exception(err)) {
        /* The device answered: it is alive, the request was wrong. */
        return MODBUS_RESULT_EXCEPTION;
    }

    switch (err) {
    case NMBS_ERROR_CRC:
    case NMBS_ERROR_INVALID_RESPONSE:
    case NMBS_ERROR_INVALID_TCP_MBAP:
        return MODBUS_RESULT_CRC;

    case NMBS_ERROR_TIMEOUT:
    case NMBS_ERROR_TRANSPORT:
        /* No usable answer reached the application. A transport failure counts
         * as a communication failure so the slave is degraded and backed off. */
        return MODBUS_RESULT_TIMEOUT;

    default:
        return MODBUS_RESULT_INVALID;
    }
}

/**
 * @brief Inter-byte (t1.5) timeout for the current line speed.
 *
 * nanoMODBUS only applies the read timeout to the first byte of a response;
 * every following byte waits for the inter-byte timeout. Leaving that at -1
 * (infinite) turns a truncated frame into a permanently hung polling thread
 * and a slave whose claim is never released. 1.5 RTU characters of 11 bits,
 * plus margin, never below 5 ms.
 */
static int32_t byte_timeout_ms(const adapter_modbus_nmbs_t *self)
{
    uint32_t baud = (self->cfg.baudrate >= 1200U) ? self->cfg.baudrate : 1200U;
    uint32_t calculated = (16500U / baud) + 2U;

    return (int32_t) ((calculated < 5U) ? 5U : calculated);
}

static enum uart_config_parity to_uart_parity(modbus_parity_t parity)
{
    switch (parity) {
    case MODBUS_PARITY_EVEN:
        return UART_CFG_PARITY_EVEN;
    case MODBUS_PARITY_ODD:
        return UART_CFG_PARITY_ODD;
    case MODBUS_PARITY_NONE:
    default:
        return UART_CFG_PARITY_NONE;
    }
}

static modbus_transaction_result_t read_holding_registers(void *impl, uint8_t slave_addr,
                                                          uint16_t reg_addr, uint16_t quantity,
                                                          uint16_t *out, int32_t timeout_ms)
{
    adapter_modbus_nmbs_t *self = (adapter_modbus_nmbs_t *) impl;

    if (self == NULL || !self->is_ready || out == NULL) {
        return MODBUS_RESULT_INVALID;
    }

    int err = nmbs_z_client_set_timeouts(&self->client, timeout_ms, byte_timeout_ms(self));

    if (err != 0) {
        return MODBUS_RESULT_INVALID;
    }

    err = nmbs_z_client_read_holding_registers_addr(&self->client, slave_addr, reg_addr, quantity,
                                                    out, K_FOREVER);

    if (err == 0) {
        return MODBUS_RESULT_OK;
    }

    return classify(nmbs_z_client_get_last_error(&self->client));
}

static modbus_transaction_result_t write_single_register(void *impl, uint8_t slave_addr,
                                                         uint16_t reg_addr, uint16_t value,
                                                         int32_t timeout_ms)
{
    adapter_modbus_nmbs_t *self = (adapter_modbus_nmbs_t *) impl;

    if (self == NULL || !self->is_ready) {
        return MODBUS_RESULT_INVALID;
    }

    int err = nmbs_z_client_set_timeouts(&self->client, timeout_ms, byte_timeout_ms(self));

    if (err != 0) {
        return MODBUS_RESULT_INVALID;
    }

    err = nmbs_z_client_write_single_register_addr(&self->client, slave_addr, reg_addr, value,
                                                   K_FOREVER);

    if (err == 0) {
        return MODBUS_RESULT_OK;
    }

    return classify(nmbs_z_client_get_last_error(&self->client));
}

static int set_bus_cfg(void *impl, const modbus_bus_cfg_t *cfg)
{
    adapter_modbus_nmbs_t *self = (adapter_modbus_nmbs_t *) impl;

    if (self == NULL || !self->is_ready || !modbus_bus_cfg_is_valid(cfg)) {
        return -EINVAL;
    }

    /* nmbs_z_serial_transport_reconfigure() only copies baudrate, parity, stop
     * bits and data bits into the transport; the flow-control field of this
     * structure is ignored, so the RS-485 hardware driver-enable configured in
     * devicetree stays in force. Do not set it here, it would be misleading. */
    const struct uart_config uart_cfg = {
        .baudrate = cfg->baudrate,
        .parity = to_uart_parity(cfg->parity),
        .stop_bits = (cfg->stop_bits == 2U) ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
    };

    int err = nmbs_z_serial_transport_reconfigure(&self->transport, &uart_cfg);

    if (err != 0) {
        LOG_ERR("reconfigure failed (%d)", err);
        return err;
    }

    self->cfg = *cfg;

    return 0;
}

static int get_bus_cfg(void *impl, modbus_bus_cfg_t *out)
{
    adapter_modbus_nmbs_t *self = (adapter_modbus_nmbs_t *) impl;

    if (self == NULL || out == NULL || !self->is_ready) {
        return -EINVAL;
    }

    *out = self->cfg;

    return 0;
}

static const i_modbus_client_api_t k_adapter_api = {
    .read_holding_registers = read_holding_registers,
    .write_single_register = write_single_register,
    .set_bus_cfg = set_bus_cfg,
    .get_bus_cfg = get_bus_cfg,
};

int adapter_modbus_nmbs_init(adapter_modbus_nmbs_t *self,
                             const nmbs_z_serial_transport_config_t *cfg)
{
    if (self == NULL || cfg == NULL || cfg->uart_dev == NULL) {
        return -EINVAL;
    }

    (void) memset(self, 0, sizeof(*self));

    int err = nmbs_z_serial_transport_init(&self->transport, cfg, &self->platform);

    if (err != 0) {
        LOG_ERR("transport init failed (%d)", err);
        return err;
    }

    err = nmbs_z_client_init(&self->client, &self->platform);

    if (err != 0) {
        LOG_ERR("client init failed (%d)", err);
        return err;
    }

    self->uart_dev = cfg->uart_dev;
    self->cfg.baudrate = cfg->baud_rate;
    self->cfg.parity = (cfg->parity == UART_CFG_PARITY_EVEN)
                           ? MODBUS_PARITY_EVEN
                           : ((cfg->parity == UART_CFG_PARITY_ODD) ? MODBUS_PARITY_ODD
                                                                   : MODBUS_PARITY_NONE);
    self->cfg.stop_bits = (cfg->stop_bits == UART_CFG_STOP_BITS_2) ? 2U : 1U;
    self->cfg.data_bits = 8U;
    self->is_ready = true;

    return 0;
}

int adapter_modbus_nmbs_port(adapter_modbus_nmbs_t *self, i_modbus_client_t *out)
{
    if (self == NULL || out == NULL || !self->is_ready) {
        return -EINVAL;
    }

    out->api = &k_adapter_api;
    out->impl = self;

    return 0;
}
