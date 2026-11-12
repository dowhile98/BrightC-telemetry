/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_modbus_tcp.c
 * @brief Active Object implementation for Industrial Modbus TCP Server.
 *
 * Wraps nanoMODBUS Zephyr TCP Server (nmbs_z_server_tcp_t) behind the
 * i_modbus_tcp_server_t interface contract.
 */

#include "ao_modbus_tcp.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_modbus_tcp, LOG_LEVEL_INF);

/* ========================================================================= */
/* Port Interface (i_modbus_tcp_server_api_t) Implementation                */
/* ========================================================================= */

static int api_init(void *impl, const modbus_tcp_server_config_t *config)
{
    ao_modbus_tcp_t *self = (ao_modbus_tcp_t *)impl;
    if (self == NULL || config == NULL) {
        return -EINVAL;
    }
    self->config = *config;
    return 0;
}

static int api_start(void *impl)
{
    return ao_modbus_tcp_start((ao_modbus_tcp_t *)impl);
}

static int api_stop(void *impl)
{
    return ao_modbus_tcp_stop((ao_modbus_tcp_t *)impl);
}

static int api_get_stats(void *impl, modbus_tcp_server_stats_t *out_stats)
{
    return ao_modbus_tcp_get_stats((ao_modbus_tcp_t *)impl, out_stats);
}

static bool api_is_running(void *impl)
{
    return ao_modbus_tcp_is_running((const ao_modbus_tcp_t *)impl);
}

static int api_get_state(void *impl, modbus_tcp_server_state_t *out_state)
{
    return ao_modbus_tcp_get_state((const ao_modbus_tcp_t *)impl, out_state);
}

static int api_get_session_info(void *impl, uint8_t slot_index, modbus_tcp_session_info_t *out_info)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(slot_index);
    ARG_UNUSED(out_info);
    return -ENOTSUP;
}

static int api_disconnect_client(void *impl, uint8_t slot_index)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(slot_index);
    return -ENOTSUP;
}

static const i_modbus_tcp_server_api_t s_modbus_tcp_api = {
    .init              = api_init,
    .start             = api_start,
    .stop              = api_stop,
    .get_stats         = api_get_stats,
    .is_running        = api_is_running,
    .get_state         = api_get_state,
    .get_session_info  = api_get_session_info,
    .disconnect_client = api_disconnect_client,
};

i_modbus_tcp_server_t ao_modbus_tcp_get_port(ao_modbus_tcp_t *self)
{
    i_modbus_tcp_server_t port = {
        .api  = &s_modbus_tcp_api,
        .impl = self,
    };
    return port;
}

/* ========================================================================= */
/* Public Lifecycle API                                                      */
/* ========================================================================= */

int ao_modbus_tcp_init(ao_modbus_tcp_t *self,
                       const modbus_tcp_server_config_t *config,
                       const nmbs_callbacks *callbacks)
{
    if (self == NULL || config == NULL || callbacks == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->config = *config;
    if (self->config.port == 0U) {
        self->config.port = MODBUS_TCP_DEFAULT_PORT;
    }

    int err = nmbs_z_server_tcp_init(&self->server, callbacks, self->config.port);
    if (err != 0) {
        LOG_ERR("Failed to initialize nanoMODBUS TCP server: %d", err);
        return err;
    }

    self->port = ao_modbus_tcp_get_port(self);
    self->state = MODBUS_TCP_STATE_STOPPED;
    self->is_initialized = true;

    LOG_INF("Modbus TCP Server initialized via nmbs_z_server_tcp on port %u",
            self->config.port);
    return 0;
}

int ao_modbus_tcp_start(ao_modbus_tcp_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    int err = nmbs_z_server_tcp_start(&self->server, NULL, 0);
    if (err == 0) {
        self->state = MODBUS_TCP_STATE_LISTENING;
        LOG_INF("Modbus TCP Server started on port %u", self->config.port);
    } else if (err == -EALREADY) {
        LOG_WRN("Modbus TCP Server is already running");
    } else {
        LOG_ERR("Failed to start Modbus TCP Server: %d", err);
        self->state = MODBUS_TCP_STATE_ERROR;
    }
    return err;
}

int ao_modbus_tcp_stop(ao_modbus_tcp_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    int err = nmbs_z_server_tcp_stop(&self->server);
    if (err == 0) {
        self->state = MODBUS_TCP_STATE_STOPPED;
        LOG_INF("Modbus TCP Server stopped cleanly");
    } else {
        LOG_WRN("nmbs_z_server_tcp_stop returned %d", err);
    }
    return err;
}

int ao_modbus_tcp_get_stats(ao_modbus_tcp_t *self, modbus_tcp_server_stats_t *out_stats)
{
    if (self == NULL || out_stats == NULL) {
        return -EINVAL;
    }

    struct nmbs_z_stats z_stats;
    int err = nmbs_z_server_tcp_get_stats(&self->server, &z_stats);
    if (err != 0) {
        return err;
    }

    out_stats->rx_frames = z_stats.rx_frames;
    out_stats->tx_frames = z_stats.tx_frames;
    out_stats->crc_errors = z_stats.crc_errors;
    out_stats->exceptions = z_stats.exceptions;
    out_stats->timeouts = z_stats.timeouts;
    return 0;
}

bool ao_modbus_tcp_is_running(const ao_modbus_tcp_t *self)
{
    if (self == NULL) {
        return false;
    }
    return (atomic_get(&self->server.running) != 0);
}

int ao_modbus_tcp_get_state(const ao_modbus_tcp_t *self, modbus_tcp_server_state_t *out_state)
{
    if (self == NULL || out_state == NULL) {
        return -EINVAL;
    }

    if (!self->is_initialized) {
        *out_state = MODBUS_TCP_STATE_UNINITIALIZED;
    } else if (atomic_get(&self->server.running) != 0) {
        *out_state = MODBUS_TCP_STATE_LISTENING;
    } else {
        *out_state = self->state;
    }
    return 0;
}
