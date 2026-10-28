/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Composition root: the single place where every object of the application is
 * created, wired and started. Nothing else in the firmware holds mutable
 * static data; the only static instance is the container declared in main.c.
 */

#ifndef APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_
#define APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include "application/active_objects/ao_modbus.h"
#include "application/modbus/modbus_registry.h"
#include "common/modbus_types.h"
#include "interfaces/i_modbus_client.h"
#include "interfaces/i_slave_registry.h"

#ifdef CONFIG_NANOMODBUS
#include "infrastructure/adapters/adapter_modbus_nmbs.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Init mask bits. */
#define APP_CONTAINER_INIT_REGISTRY 0x01U
#define APP_CONTAINER_INIT_AO 0x02U
#define APP_CONTAINER_INIT_TRANSPORT 0x04U

/**
 * @brief Every object of the application, wired together.
 *
 * Declared once (`static app_container_t g_app` in main.c) and passed by
 * reference to every component. Adapters live here by value so that no module
 * needs file-scope mutable state.
 *
 * The container must be zero-initialised before the first call to
 * composition_root_init(): either declare it `static` or `memset` it first.
 * Thread stacks are defined separately at file scope with K_THREAD_STACK_ARRAY_DEFINE
 * to satisfy MPU alignment and guard constraints.
 */
typedef struct {
    /* Ports exposed to the rest of the application */
    i_slave_registry_t registry;              /**< Slave registry port */
    i_modbus_client_t client[MODBUS_BUS_MAX]; /**< One client port per bus */

    /* Adapters, stored by value */
    modbus_registry_t registry_impl;          /**< Concrete registry */
#ifdef CONFIG_NANOMODBUS
    adapter_modbus_nmbs_t nmbs[MODBUS_BUS_MAX]; /**< nanoMODBUS client per bus */
#endif

    /* Active objects and their static resources */
    ao_modbus_t ao[MODBUS_BUS_MAX];             /**< One polling thread per bus */
    struct k_msgq ao_queue[MODBUS_BUS_MAX];     /**< Command queues */
    uint8_t ao_queue_buf[MODBUS_BUS_MAX][AO_MODBUS_QUEUE_DEPTH * sizeof(modbus_event_t)];

    /* Configuration of the physical buses */
    modbus_bus_cfg_t bus_cfg[MODBUS_BUS_MAX];

    uint8_t init_mask; /**< APP_CONTAINER_INIT_* bits already applied */
} app_container_t;

/**
 * @brief Create and wire every object. Idempotent.
 *
 * @param[out] self Container to initialise.
 *
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init(app_container_t *self);

/**
 * @brief Start the Active Objects. Idempotent.
 *
 * @param[in,out] self Container previously initialised.
 *
 * @return 0 on success, -EINVAL when @p self is not initialised.
 */
int composition_root_start(app_container_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_ */
