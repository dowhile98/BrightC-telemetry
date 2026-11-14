/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_event_mediator.h
 * @brief Inter-Subsystem Event Mediator and Zbus Listener Hub.
 *
 * Decouples cross-subsystem event routing and Zbus listener callbacks
 * from the dependency injection composition root.
 */

#ifndef APP_INFRASTRUCTURE_DI_APP_EVENT_MEDIATOR_H_
#define APP_INFRASTRUCTURE_DI_APP_EVENT_MEDIATOR_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of app_container_t */
typedef struct app_container app_container_t;

/**
 * @brief Bind active DI container to Zbus event mediator listeners.
 *
 * @param[in] container Pointer to initialized app_container_t instance.
 * @return 0 on success, -EINVAL on null pointer.
 */
int app_event_mediator_bind(app_container_t *container);

/**
 * @brief Unbind active DI container from Zbus event mediator.
 */
void app_event_mediator_unbind(void);

/**
 * @brief Query current bound DI container instance.
 *
 * @return Pointer to container, or NULL if unbound.
 */
app_container_t *app_event_mediator_get_container(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_DI_APP_EVENT_MEDIATOR_H_ */
