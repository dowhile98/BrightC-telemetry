/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi concrete adapter for ESP-Hosted-MCU (SPI4) behind i_network.
 */

#include "infrastructure/adapters/adapter_wifi_esp_hosted.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(adapter_wifi_esp_hosted, LOG_LEVEL_INF);

static enum wifi_security_type map_security_to_zephyr(net_wifi_security_t sec)
{
    switch (sec) {
    case NET_WIFI_SEC_OPEN:
        return WIFI_SECURITY_TYPE_NONE;
    case NET_WIFI_SEC_WPA_PSK:
        return WIFI_SECURITY_TYPE_WPA_PSK;
    case NET_WIFI_SEC_WPA2_PSK:
        return WIFI_SECURITY_TYPE_PSK;
    case NET_WIFI_SEC_WPA3_SAE:
        return WIFI_SECURITY_TYPE_SAE;
    default:
        return WIFI_SECURITY_TYPE_NONE;
    }
}

static int start_sta(adapter_wifi_esp_hosted_t *ctx)
{
    if (ctx->sta_iface == NULL) {
        LOG_ERR("WiFi: STA interface not available");
        return -ENODEV;
    }

    struct wifi_connect_req_params params;
    (void)memset(&params, 0, sizeof(params));

    params.ssid = (const uint8_t *)ctx->cfg.sta.ssid;
    params.ssid_length = (uint8_t)strlen(ctx->cfg.sta.ssid);

    if (ctx->cfg.sta.security != NET_WIFI_SEC_OPEN) {
        params.psk = (const uint8_t *)ctx->cfg.sta.psk;
        params.psk_length = (uint8_t)strlen(ctx->cfg.sta.psk);
    }

    params.security = map_security_to_zephyr((net_wifi_security_t)ctx->cfg.sta.security);
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    params.timeout = SYS_FOREVER_MS;

    LOG_INF("WiFi: connecting STA to SSID '%s'", ctx->cfg.sta.ssid);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, ctx->sta_iface, &params, sizeof(params));
    if (ret != 0) {
        LOG_ERR("WiFi: NET_REQUEST_WIFI_CONNECT failed (%d)", ret);
        return ret;
    }

    if (ctx->cfg.sta.ip_mode == NET_IP_MODE_DHCP) {
#if defined(CONFIG_NET_DHCPV4)
        LOG_INF("WiFi: starting DHCPv4 on STA interface");
        net_dhcpv4_start(ctx->sta_iface);
#endif
    } else if (ctx->cfg.sta.ip_mode == NET_IP_MODE_STATIC) {
        struct net_in_addr ip;
        struct net_in_addr nm;
        struct net_in_addr gw;

        ip.s_addr = net_htonl(ctx->cfg.sta.ip_addr);
        nm.s_addr = net_htonl(ctx->cfg.sta.subnet_mask);
        gw.s_addr = net_htonl(ctx->cfg.sta.gateway);

        (void)net_if_ipv4_addr_add(ctx->sta_iface, &ip, NET_ADDR_MANUAL, 0);
        (void)net_if_ipv4_set_netmask_by_addr(ctx->sta_iface, &ip, &nm);
        net_if_ipv4_set_gw(ctx->sta_iface, &gw);
    }

    return 0;
}

static int start_ap(adapter_wifi_esp_hosted_t *ctx)
{
    struct net_if *target_iface = (ctx->ap_iface != NULL) ? ctx->ap_iface : ctx->sta_iface;
    if (target_iface == NULL) {
        LOG_ERR("WiFi: AP interface not available");
        return -ENODEV;
    }

    struct wifi_connect_req_params params;
    (void)memset(&params, 0, sizeof(params));

    params.ssid = (const uint8_t *)ctx->cfg.ap.ssid;
    params.ssid_length = (uint8_t)strlen(ctx->cfg.ap.ssid);

    if (ctx->cfg.ap.security != NET_WIFI_SEC_OPEN) {
        params.psk = (const uint8_t *)ctx->cfg.ap.psk;
        params.psk_length = (uint8_t)strlen(ctx->cfg.ap.psk);
    }

    params.security = map_security_to_zephyr((net_wifi_security_t)ctx->cfg.ap.security);
    params.channel = (ctx->cfg.ap.channel != 0) ? ctx->cfg.ap.channel : 9U;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    LOG_INF("WiFi: enabling Soft-AP with SSID '%s', channel %u", ctx->cfg.ap.ssid, params.channel);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, target_iface, &params, sizeof(params));
    if (ret != 0) {
        LOG_ERR("WiFi: NET_REQUEST_WIFI_AP_ENABLE failed (%d)", ret);
        return ret;
    }

    /* Configure AP static gateway IP */
    if (ctx->cfg.ap.ap_ip_addr != 0) {
        struct net_in_addr ip;
        struct net_in_addr nm;

        ip.s_addr = net_htonl(ctx->cfg.ap.ap_ip_addr);
        nm.s_addr = net_htonl(ctx->cfg.ap.ap_subnet_mask);

        (void)net_if_ipv4_addr_add(target_iface, &ip, NET_ADDR_MANUAL, 0);
        (void)net_if_ipv4_set_netmask_by_addr(target_iface, &ip, &nm);
        net_if_ipv4_set_gw(target_iface, &ip);
    }

    return 0;
}

static int stop_sta(adapter_wifi_esp_hosted_t *ctx)
{
    if (ctx->sta_iface == NULL) {
        return 0;
    }

#if defined(CONFIG_NET_DHCPV4)
    if (ctx->cfg.sta.ip_mode == NET_IP_MODE_DHCP) {
        net_dhcpv4_stop(ctx->sta_iface);
    }
#endif

    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, ctx->sta_iface, NULL, 0);
    return ret;
}

static int stop_ap(adapter_wifi_esp_hosted_t *ctx)
{
    struct net_if *target_iface = (ctx->ap_iface != NULL) ? ctx->ap_iface : ctx->sta_iface;
    if (target_iface == NULL) {
        return 0;
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, target_iface, NULL, 0);
    return ret;
}

static int wifi_api_init(void *impl, network_interface_t iface)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI) {
        return -EINVAL;
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.ip_addr = 0;
    ctx->status.subnet_mask = 0;
    ctx->status.gateway = 0;
    ctx->status.rssi = -128;
    ctx->status.link_up_ms = 0;
    ctx->link_up_time = 0;

    return 0;
}

static int wifi_api_start(void *impl, network_interface_t iface)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI) {
        return -EINVAL;
    }

    int err = 0;

    switch ((net_wifi_mode_t)ctx->cfg.mode) {
    case NET_WIFI_MODE_STA:
        err = start_sta(ctx);
        break;
    case NET_WIFI_MODE_AP:
        err = start_ap(ctx);
        break;
    case NET_WIFI_MODE_STA_AP: {
        int err_ap = start_ap(ctx);
        int err_sta = start_sta(ctx);
        err = (err_ap != 0) ? err_ap : err_sta;
        break;
    }
    default:
        err = -EINVAL;
        break;
    }

    if (err == 0) {
        ctx->is_started = true;
        ctx->status.state = NET_IF_STATE_CONNECTING;
    } else {
        ctx->status.state = NET_IF_STATE_FAILED;
    }

    return err;
}

static int wifi_api_stop(void *impl, network_interface_t iface)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI) {
        return -EINVAL;
    }

    switch ((net_wifi_mode_t)ctx->cfg.mode) {
    case NET_WIFI_MODE_STA:
        (void)stop_sta(ctx);
        break;
    case NET_WIFI_MODE_AP:
        (void)stop_ap(ctx);
        break;
    case NET_WIFI_MODE_STA_AP:
        (void)stop_sta(ctx);
        (void)stop_ap(ctx);
        break;
    default:
        break;
    }

    ctx->is_started = false;
    ctx->status.state = NET_IF_STATE_DOWN;
    ctx->status.link_up_ms = 0;
    ctx->link_up_time = 0;

    return 0;
}

static int wifi_api_get_config(void *impl, network_interface_t iface,
                              void *out_data, size_t max_size, size_t *out_size)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI || out_data == NULL) {
        return -EINVAL;
    }

    if (max_size < sizeof(net_wifi_config_t)) {
        return -ENOMEM;
    }

    (void)memcpy(out_data, &ctx->cfg, sizeof(net_wifi_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_wifi_config_t);
    }

    return 0;
}

static int wifi_api_set_config(void *impl, network_interface_t iface,
                              const void *data, size_t size)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI || data == NULL) {
        return -EINVAL;
    }

    if (size != sizeof(net_wifi_config_t)) {
        return -EINVAL;
    }

    const net_wifi_config_t *new_cfg = (const net_wifi_config_t *)data;
    if (!net_wifi_config_is_valid(new_cfg)) {
        return -EINVAL;
    }

    uint8_t saved_mac[NET_MAC_ADDR_LEN];
    (void)memcpy(saved_mac, ctx->cfg.mac_addr, sizeof(saved_mac));

    (void)memcpy(&ctx->cfg, new_cfg, sizeof(net_wifi_config_t));
    (void)memcpy(ctx->cfg.mac_addr, saved_mac, sizeof(ctx->cfg.mac_addr));

    return 0;
}

static int wifi_api_get_status(void *impl, network_interface_t iface,
                              net_status_t *out_status)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI || out_status == NULL) {
        return -EINVAL;
    }

    struct net_if *active_iface = (ctx->cfg.mode == NET_WIFI_MODE_AP && ctx->ap_iface != NULL)
                                      ? ctx->ap_iface
                                      : ctx->sta_iface;

    if (active_iface == NULL) {
        ctx->status.state = NET_IF_STATE_FAILED;
        *out_status = ctx->status;
        return 0;
    }

    if (!ctx->is_started) {
        ctx->status.state = NET_IF_STATE_DOWN;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->status.rssi = -128;
        ctx->status.link_up_ms = 0;
        ctx->link_up_time = 0;
        *out_status = ctx->status;
        return 0;
    }

    struct wifi_iface_status zephyr_status;
    (void)memset(&zephyr_status, 0, sizeof(zephyr_status));

    int ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, active_iface,
                       &zephyr_status, sizeof(zephyr_status));
    if (ret == 0) {
        ctx->status.rssi = (int8_t)zephyr_status.rssi;
    } else {
        ctx->status.rssi = -128;
    }

    uint32_t now = (uint32_t)k_uptime_get();
    struct net_in_addr *addr = net_if_ipv4_get_global_addr(active_iface, NET_ADDR_PREFERRED);

    if (addr != NULL && addr->s_addr != 0) {
        ctx->status.state = NET_IF_STATE_CONNECTED;
        ctx->status.ip_addr = net_ntohl(addr->s_addr);

        struct net_in_addr nm = net_if_ipv4_get_netmask_by_addr(active_iface, addr);
        ctx->status.subnet_mask = net_ntohl(nm.s_addr);

        struct net_in_addr gw = net_if_ipv4_get_gw(active_iface);
        ctx->status.gateway = net_ntohl(gw.s_addr);

        if (ctx->link_up_time == 0) {
            ctx->link_up_time = now;
        }
        ctx->status.link_up_ms = now - ctx->link_up_time;
    } else if (zephyr_status.state == WIFI_STATE_COMPLETED ||
               zephyr_status.state == WIFI_SAP_IFACE_ENABLED) {
        ctx->status.state = NET_IF_STATE_CONNECTING;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
    } else if (zephyr_status.state == WIFI_STATE_DISCONNECTED ||
               zephyr_status.state == WIFI_SAP_IFACE_DISABLED) {
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->status.link_up_ms = 0;
        ctx->link_up_time = 0;
    } else {
        ctx->status.state = NET_IF_STATE_CONNECTING;
    }

    *out_status = ctx->status;
    return 0;
}

static int wifi_api_scan(void *impl, network_interface_t iface)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_WIFI) {
        return -EINVAL;
    }

    if (ctx->sta_iface == NULL) {
        return -ENODEV;
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, ctx->sta_iface, NULL, 0);
    return ret;
}

static const i_network_api_t s_adapter_wifi_esp_hosted_api = {
    .init = wifi_api_init,
    .start = wifi_api_start,
    .stop = wifi_api_stop,
    .get_config = wifi_api_get_config,
    .set_config = wifi_api_set_config,
    .get_status = wifi_api_get_status,
    .scan = wifi_api_scan,
};

int adapter_wifi_esp_hosted_init(adapter_wifi_esp_hosted_t *ctx, const struct device *wifi_dev)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->wifi_dev = wifi_dev;

    (void)network_default_wifi_config(&ctx->cfg);

    /* Locate STA interface */
#if defined(CONFIG_WIFI)
    ctx->sta_iface = net_if_get_wifi_sta();
#endif

    if (ctx->sta_iface == NULL && wifi_dev != NULL && device_is_ready(wifi_dev)) {
        ctx->sta_iface = net_if_lookup_by_dev(wifi_dev);
    }

    /* Locate Soft-AP interface if supported */
#if defined(CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE)
    ctx->ap_iface = net_if_get_wifi_sap();
#endif

    if (ctx->sta_iface != NULL) {
        struct net_linkaddr *link_addr = net_if_get_link_addr(ctx->sta_iface);
        if (link_addr != NULL && link_addr->len >= NET_MAC_ADDR_LEN) {
            (void)memcpy(ctx->cfg.mac_addr, link_addr->addr, NET_MAC_ADDR_LEN);
        }
    } else {
        LOG_WRN("WiFi: STA net_if not found at init");
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.rssi = -128;
    ctx->is_ready = true;
    ctx->is_started = false;

    return 0;
}

i_network_t adapter_wifi_esp_hosted_port(adapter_wifi_esp_hosted_t *ctx)
{
    i_network_t port = {
        .api = (ctx != NULL && ctx->is_ready) ? &s_adapter_wifi_esp_hosted_api : NULL,
        .impl = (void *)ctx,
    };
    return port;
}
