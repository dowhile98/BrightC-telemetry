/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port interface for embedded HTTP web server and REST API service.
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_web_server_api_t)
 * 2) Handle (i_web_server_t)
 * 3) Validated wrappers (static inline i_web_server_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero vendor HAL, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_WEB_SERVER_H_
#define APP_INTERFACES_I_WEB_SERVER_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/web_server_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /**
     * @brief Initialize web server adapter context with runtime configuration.
     */
    int (*init)(void *impl, const web_server_config_t *config);

    /**
     * @brief Start HTTP listener service and client processing.
     */
    int (*start)(void *impl);

    /**
     * @brief Stop HTTP listener service and disconnect active clients.
     */
    int (*stop)(void *impl);

    /**
     * @brief Query cumulative server metrics and operational state.
     */
    int (*get_metrics)(void *impl, web_server_metrics_t *out_metrics);

    /**
     * @brief Notify web server of MicroSD mount state change.
     * Transitions server between normal static FS mode and degraded Flash fallback mode.
     */
    int (*notify_sd_status)(void *impl, bool is_mounted);
} i_web_server_api_t;

/*============================================================================*
 * 2. PORT HANDLE (injected into consumers / composition root)
 *============================================================================*/

typedef struct {
    const i_web_server_api_t *api; /**< V-table pointer */
    void *impl;                    /**< Concrete adapter context pointer */
} i_web_server_t;

/**
 * @brief Check that a web server port handle is bound.
 *
 * @param[in] srv Port handle to check (may be NULL).
 * @return true if both api and impl pointers are non-NULL and core operations are present.
 */
static inline bool i_web_server_is_bound(const i_web_server_t *srv)
{
    return (srv != NULL &&
            srv->api != NULL &&
            srv->impl != NULL &&
            srv->api->init != NULL &&
            srv->api->start != NULL &&
            srv->api->stop != NULL &&
            srv->api->get_metrics != NULL &&
            srv->api->notify_sd_status != NULL);
}

/*============================================================================*
 * 3. VALIDATED WRAPPERS (public API called by application & active objects)
 *============================================================================*/

/**
 * @brief Initialize the embedded web server adapter.
 */
static inline int i_web_server_init(const i_web_server_t *srv, const web_server_config_t *config)
{
    if (!i_web_server_is_bound(srv) || config == NULL) {
        return -EINVAL;
    }
    return srv->api->init(srv->impl, config);
}

/**
 * @brief Start the HTTP web server.
 */
static inline int i_web_server_start(const i_web_server_t *srv)
{
    if (!i_web_server_is_bound(srv)) {
        return -EINVAL;
    }
    return srv->api->start(srv->impl);
}

/**
 * @brief Stop the HTTP web server.
 */
static inline int i_web_server_stop(const i_web_server_t *srv)
{
    if (!i_web_server_is_bound(srv)) {
        return -EINVAL;
    }
    return srv->api->stop(srv->impl);
}

/**
 * @brief Retrieve current metrics from the web server.
 */
static inline int i_web_server_get_metrics(const i_web_server_t *srv, web_server_metrics_t *out_metrics)
{
    if (!i_web_server_is_bound(srv) || out_metrics == NULL) {
        return -EINVAL;
    }
    return srv->api->get_metrics(srv->impl, out_metrics);
}

/**
 * @brief Notify the web server of SD card attachment/detachment.
 */
static inline int i_web_server_notify_sd_status(const i_web_server_t *srv, bool is_mounted)
{
    if (!i_web_server_is_bound(srv)) {
        return -EINVAL;
    }
    return srv->api->notify_sd_status(srv->impl, is_mounted);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_WEB_SERVER_H_ */
