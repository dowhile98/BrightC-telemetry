/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_facade_network.c
 * @brief Application Service Facade implementation for Network & Wi-Fi/Ethernet subsystem.
 */

#include "application/services/app_facade.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "infrastructure/di/composition_root.h"
#include "application/active_objects/ao_network.h"
#include "application/active_objects/ao_storage.h"

#ifndef APP_FACADE_CONTAINER_DEFINED
static app_container_t *s_app = NULL;

int app_facade_init(void *container)
{
    if (container == NULL) {
        return -EINVAL;
    }
    s_app = (app_container_t *)container;
    return 0;
}
#endif

/* =========================================================================
 * Network Service APIs
 * ========================================================================= */

int app_facade_network_get_status(network_interface_t iface, net_status_t *out_status)
{
    if (s_app == NULL || out_status == NULL || !network_interface_is_valid(iface)) {
        return -EINVAL;
    }

    const i_network_t *net = (iface == NETWORK_INTERFACE_ETHERNET)
                                 ? app_container_get_network_eth(s_app)
                                 : app_container_get_network_wifi(s_app);
    if (net == NULL) {
        return -ENODEV;
    }

    return i_network_get_status(net, out_status);
}

int app_facade_network_get_eth_config(net_eth_config_t *out_cfg)
{
    if (s_app == NULL || out_cfg == NULL) {
        return -EINVAL;
    }

    ao_network_t *net_ao = app_container_get_network_ao(s_app);
    if (net_ao == NULL) {
        return -ENODEV;
    }

    return ao_network_get_eth_config(net_ao, out_cfg);
}

int app_facade_network_get_wifi_config(net_wifi_config_t *out_cfg)
{
    if (s_app == NULL || out_cfg == NULL) {
        return -EINVAL;
    }

    ao_network_t *net_ao = app_container_get_network_ao(s_app);
    if (net_ao == NULL) {
        return -ENODEV;
    }

    return ao_network_get_wifi_config(net_ao, out_cfg);
}

int app_facade_network_eth_set_dhcp(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_network_t *net_ao = app_container_get_network_ao(s_app);
    if (net_ao == NULL) {
        return -ENODEV;
    }

    net_eth_config_t cfg;
    int rc = ao_network_get_eth_config(net_ao, &cfg);
    if (rc != 0) {
        (void)network_default_eth_config(&cfg);
    }

    cfg.ip_mode = (uint8_t)NET_IP_MODE_DHCP;
    cfg.auto_dhcpv4 = 1U;

    if (!net_eth_config_is_valid(&cfg)) {
        return -EINVAL;
    }

    ao_network_event_t evt = {
        .type = (uint8_t)AO_NETWORK_EVENT_CONFIG_ETH,
        .data.eth_cfg = cfg,
    };

    rc = ao_network_post(net_ao, &evt);
    if (rc != 0) {
        return rc;
    }

    ao_storage_t *storage_ao = app_container_get_storage_ao(s_app);
    if (storage_ao != NULL) {
        (void)ao_storage_mark_dirty(storage_ao, CONFIG_DOMAIN_NET_ETH, 0U);
    }

    return 0;
}

int app_facade_network_eth_set_static(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_network_t *net_ao = app_container_get_network_ao(s_app);
    if (net_ao == NULL) {
        return -ENODEV;
    }

    net_eth_config_t cfg;
    int rc = ao_network_get_eth_config(net_ao, &cfg);
    if (rc != 0) {
        (void)network_default_eth_config(&cfg);
    }

    cfg.ip_mode = (uint8_t)NET_IP_MODE_STATIC;
    cfg.auto_dhcpv4 = 0U;
    cfg.ip_addr = ip;
    cfg.subnet_mask = netmask;
    cfg.gateway = gateway;

    if (!net_eth_config_is_valid(&cfg)) {
        return -EINVAL;
    }

    ao_network_event_t evt = {
        .type = (uint8_t)AO_NETWORK_EVENT_CONFIG_ETH,
        .data.eth_cfg = cfg,
    };

    rc = ao_network_post(net_ao, &evt);
    if (rc != 0) {
        return rc;
    }

    ao_storage_t *storage_ao = app_container_get_storage_ao(s_app);
    if (storage_ao != NULL) {
        (void)ao_storage_mark_dirty(storage_ao, CONFIG_DOMAIN_NET_ETH, 0U);
    }

    return 0;
}

int app_facade_network_wifi_scan(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    const i_wifi_control_t *wifi = app_container_get_wifi_control(s_app);
    if (wifi == NULL) {
        return -ENODEV;
    }

    return i_wifi_control_scan(wifi);
}

int app_facade_network_wifi_connect(const char *ssid, const char *psk, net_wifi_security_t sec)
{
    if (s_app == NULL || ssid == NULL || !net_wifi_security_is_valid(sec)) {
        return -EINVAL;
    }
    if (sec != NET_WIFI_SEC_OPEN && psk == NULL) {
        return -EINVAL;
    }

    ao_network_t *net_ao = app_container_get_network_ao(s_app);
    if (net_ao == NULL) {
        return -ENODEV;
    }

    net_wifi_config_t cfg;
    int rc = ao_network_get_wifi_config(net_ao, &cfg);
    if (rc != 0) {
        (void)network_default_wifi_config(&cfg);
    }

    net_wifi_mode_t current_mode = (net_wifi_mode_t)cfg.mode;
    cfg.mode = (current_mode == NET_WIFI_MODE_AP || current_mode == NET_WIFI_MODE_STA_AP)
                   ? (uint8_t)NET_WIFI_MODE_STA_AP
                   : (uint8_t)NET_WIFI_MODE_STA;

    (void)memset(cfg.sta.ssid, 0, sizeof(cfg.sta.ssid));
    strncpy(cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1U);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1U] = '\0';

    (void)memset(cfg.sta.psk, 0, sizeof(cfg.sta.psk));
    if (psk != NULL) {
        strncpy(cfg.sta.psk, psk, sizeof(cfg.sta.psk) - 1U);
        cfg.sta.psk[sizeof(cfg.sta.psk) - 1U] = '\0';
    }

    cfg.sta.security = (uint8_t)sec;
    cfg.sta.ip_mode = (uint8_t)NET_IP_MODE_DHCP;

    if (!net_wifi_config_is_valid(&cfg)) {
        return -EINVAL;
    }

    ao_network_event_t evt = {
        .type = (uint8_t)AO_NETWORK_EVENT_CONFIG_WIFI,
        .data.wifi_cfg = cfg,
    };

    rc = ao_network_post(net_ao, &evt);
    if (rc != 0) {
        return rc;
    }

    /* Also invoke connect_ap synchronously on the Wi-Fi control port */
    const i_wifi_control_t *wifi = app_container_get_wifi_control(s_app);
    if (wifi != NULL) {
        (void)i_wifi_control_connect_ap(wifi, ssid, psk, sec);
    }

    /* Mark WiFi config domain dirty so it persists to EEPROM */
    ao_storage_t *storage_ao = app_container_get_storage_ao(s_app);
    if (storage_ao != NULL) {
        (void)ao_storage_mark_dirty(storage_ao, CONFIG_DOMAIN_NET_WIFI, 0U);
    }

    return 0;
}

int app_facade_network_wifi_disconnect(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    const i_wifi_control_t *wifi = app_container_get_wifi_control(s_app);
    if (wifi == NULL) {
        return -ENODEV;
    }

    return i_wifi_control_disconnect(wifi);
}

int app_facade_network_save(void)
{
    if (s_app == NULL) {
        return -EINVAL;
    }

    ao_storage_t *storage_ao = app_container_get_storage_ao(s_app);
    if (storage_ao == NULL) {
        return -ENODEV;
    }

    return ao_storage_flush_now(storage_ao);
}

int app_facade_network_wifi_config_save(void)
{
    return app_facade_network_save();
}
