/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_wifi_control.h
 * @brief Segregated interface for Wi-Fi management and control.
 *
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md and ISP (Interface Segregation Principle):
 * 1) V-table (i_wifi_control_api_t)
 * 2) Handle (i_wifi_control_t)
 * 3) Validated wrappers (static inline i_wifi_control_*())
 *
 * Pure C99/C11 - no Zephyr headers, no vendor HAL, no dynamic memory.
 */

#ifndef APP_INTERFACES_I_WIFI_CONTROL_H_
#define APP_INTERFACES_I_WIFI_CONTROL_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/network_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /**
     * @brief Trigger wireless AP scan.
     */
    int (*scan)(void *impl);

    /**
     * @brief Connect Station to specified AP.
     */
    int (*connect_ap)(void *impl, const char *ssid, const char *psk, net_wifi_security_t sec);

    /**
     * @brief Disconnect Station from currently associated AP.
     */
    int (*disconnect)(void *impl);

    /**
     * @brief Set Wi-Fi operating mode (STA, AP, STA_AP).
     */
    int (*set_mode)(void *impl, net_wifi_mode_t mode);
} i_wifi_control_api_t;

/*============================================================================*
 * 2. PORT HANDLE (injected into consumers)
 *============================================================================*/

typedef struct {
    const i_wifi_control_api_t *api; /**< V-table pointer */
    void *impl;                      /**< Concrete adapter context pointer */
} i_wifi_control_t;

/**
 * @brief Check that a Wi-Fi control port handle is bound to a concrete implementation.
 *
 * @param[in] wifi Port handle to check (may be NULL).
 * @return true if both api and impl pointers are non-NULL and all methods are non-NULL.
 */
static inline bool i_wifi_control_is_bound(const i_wifi_control_t *wifi)
{
    return wifi != NULL && wifi->api != NULL && wifi->impl != NULL &&
           wifi->api->scan != NULL && wifi->api->connect_ap != NULL &&
           wifi->api->disconnect != NULL && wifi->api->set_mode != NULL;
}

/*============================================================================*
 * 3. VALIDATED WRAPPERS (only allowed path to dispatch through the v-table)
 *============================================================================*/

/**
 * @brief Trigger a wireless network scan.
 *
 * @param[in] wifi Bound Wi-Fi control port handle.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_wifi_control_scan(const i_wifi_control_t *wifi)
{
    if (!i_wifi_control_is_bound(wifi)) {
        return -EINVAL;
    }
    return wifi->api->scan(wifi->impl);
}

/**
 * @brief Connect Wi-Fi Station to access point.
 *
 * @param[in] wifi Bound Wi-Fi control port handle.
 * @param[in] ssid SSID string (null-terminated, non-NULL).
 * @param[in] psk  Pre-shared key (null-terminated, required unless sec == NET_WIFI_SEC_OPEN).
 * @param[in] sec  Security protocol.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_wifi_control_connect_ap(const i_wifi_control_t *wifi,
                                            const char *ssid,
                                            const char *psk,
                                            net_wifi_security_t sec)
{
    if (!i_wifi_control_is_bound(wifi) || ssid == NULL || !net_wifi_security_is_valid(sec)) {
        return -EINVAL;
    }
    if (sec != NET_WIFI_SEC_OPEN && psk == NULL) {
        return -EINVAL;
    }
    return wifi->api->connect_ap(wifi->impl, ssid, psk, sec);
}

/**
 * @brief Disconnect Wi-Fi Station from currently connected network.
 *
 * @param[in] wifi Bound Wi-Fi control port handle.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_wifi_control_disconnect(const i_wifi_control_t *wifi)
{
    if (!i_wifi_control_is_bound(wifi)) {
        return -EINVAL;
    }
    return wifi->api->disconnect(wifi->impl);
}

/**
 * @brief Change Wi-Fi operating mode.
 *
 * @param[in] wifi Bound Wi-Fi control port handle.
 * @param[in] mode Desired operating mode (STA, AP, STA_AP).
 * @return 0 on success, negative errno on failure.
 */
static inline int i_wifi_control_set_mode(const i_wifi_control_t *wifi, net_wifi_mode_t mode)
{
    if (!i_wifi_control_is_bound(wifi) || !net_wifi_mode_is_valid(mode)) {
        return -EINVAL;
    }
    return wifi->api->set_mode(wifi->impl, mode);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_WIFI_CONTROL_H_ */
