/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "composition_root.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(composition_root, LOG_LEVEL_INF);

/**
 * Line configuration applied to a bus when its transport cannot report one.
 *
 * Also the boot configuration in host builds, where no transport is compiled
 * in and the tests bind a fake client port instead.
 */
static const modbus_bus_cfg_t k_fallback_bus_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
    [MODBUS_BUS_RS485_2] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
};

#ifdef CONFIG_NANOMODBUS
/**
 * Transport configuration of each bus, taken from devicetree.
 *
 * The nodes are declared in app/boards/brightc-controllerv3.overlay:
 * `modbus-rtu-1` -> USART2 (RS485-1) and `modbus-rtu-2` -> USART6 (RS485-2).
 */
static const nmbs_z_serial_transport_config_t k_bus_transport_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_ALIAS(modbus_rtu_1)),
    [MODBUS_BUS_RS485_2] = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_ALIAS(modbus_rtu_2)),
};
#endif

/**
 * Slave table applied at boot.
 *
 * A compile-time table, not mutable state: it will be replaced by the
 * persisted configuration once the settings backend lands.
 */
static const modbus_slave_descriptor_t k_default_slaves[] = {
    {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
};

/**
 * Bring up the transport of every bus and bind its client port.
 *
 * A bus that fails to come up is left unbound and the others are still
 * started: the whole point of one polling thread per bus is that a dead wire
 * must not take the healthy one down with it. Only a controller with no
 * usable bus at all is a fatal error, because then nothing can ever be polled.
 */
static int init_transport(app_container_t *self)
{
    (void)memcpy(self->bus_cfg, k_fallback_bus_cfg, sizeof(self->bus_cfg));

#ifdef CONFIG_NANOMODBUS
    size_t bound_buses = 0U;

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        int err = adapter_modbus_nmbs_init(&self->nmbs[bus], &k_bus_transport_cfg[bus]);

        if (err == 0)
        {
            err = adapter_modbus_nmbs_port(&self->nmbs[bus], &self->client[bus]);
        }

        if (err == 0)
        {
            err = i_modbus_client_get_bus_cfg(&self->client[bus], &self->bus_cfg[bus]);
        }

        if (err != 0)
        {
            LOG_ERR("bus %u: transport unavailable (%d), continuing without it", bus, err);
            self->client[bus].api = NULL;
            self->client[bus].impl = NULL;
            continue;
        }

        bound_buses++;
    }

    if (bound_buses == 0U)
    {
        LOG_ERR("no Modbus transport could be brought up");
        return -ENODEV;
    }

    self->init_mask |= APP_CONTAINER_INIT_TRANSPORT;
#endif

    return 0;
}

static int init_registry(app_container_t *self)
{
    int err = modbus_registry_init(&self->registry_impl, self->bus_cfg);

    if (err != 0)
    {
        LOG_ERR("registry init failed (%d)", err);
        return err;
    }

    err = modbus_registry_port(&self->registry_impl, &self->registry);
    if (err != 0)
    {
        LOG_ERR("registry port binding failed (%d)", err);
        return err;
    }

    for (size_t i = 0U; i < ARRAY_SIZE(k_default_slaves); i++)
    {
        err = modbus_registry_register(&self->registry_impl, &k_default_slaves[i]);
        if (err != 0)
        {
            LOG_ERR("registering device %u failed (%d)", k_default_slaves[i].device_id, err);
            return err;
        }
    }

    self->init_mask |= APP_CONTAINER_INIT_REGISTRY;

    return 0;
}

static K_THREAD_STACK_ARRAY_DEFINE(g_ao_stacks, MODBUS_BUS_MAX, AO_MODBUS_STACK_SIZE);

static int init_active_objects(app_container_t *self)
{
    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        k_msgq_init(&self->ao_queue[bus], self->ao_queue_buf[bus], sizeof(modbus_event_t),
                    AO_MODBUS_QUEUE_DEPTH);

        int err = ao_modbus_init(&self->ao[bus], bus, &self->registry, &self->client[bus],
                                 &self->ao_queue[bus], g_ao_stacks[bus], AO_MODBUS_STACK_SIZE);

        if (err != 0)
        {
            LOG_ERR("bus %u: active object init failed (%d)", bus, err);
            return err;
        }
    }

    self->init_mask |= APP_CONTAINER_INIT_AO;

    return 0;
}

int composition_root_init(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    /* Idempotent: only a fully wired container is left alone. Requiring both
     * bits also keeps a container whose transport/registry step failed from
     * being mistaken for a finished one. */
    const uint8_t done = APP_CONTAINER_INIT_REGISTRY | APP_CONTAINER_INIT_AO;

    if ((self->init_mask & done) == done)
    {
        return 0;
    }

    (void)memset(self, 0, sizeof(*self));

    int err = init_transport(self);

    if (err != 0)
    {
        return err;
    }

    err = init_registry(self);
    if (err != 0)
    {
        return err;
    }

    return init_active_objects(self);
}

int composition_root_start(app_container_t *self)
{
    if (self == NULL || (self->init_mask & APP_CONTAINER_INIT_AO) == 0U)
    {
        return -EINVAL;
    }

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        int err = ao_modbus_start(&self->ao[bus]);

        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("bus %u: active object start failed (%d)", bus, err);
            return err;
        }
    }

    return 0;
}
