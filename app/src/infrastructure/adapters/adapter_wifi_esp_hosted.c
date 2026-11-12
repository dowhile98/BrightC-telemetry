/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi concrete adapter for ESP-Hosted-MCU (SPI4) behind i_network and i_wifi_control.
 */

#include "infrastructure/adapters/adapter_wifi_esp_hosted.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#if defined(CONFIG_NET_DHCPV4_SERVER)
#include <zephyr/net/dhcpv4_server.h>
#endif
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

    /* Bring interface up before initiating connection */
    net_if_up(ctx->sta_iface);

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

    /* Bring interface up */
    net_if_up(target_iface);

    /* Configure AP static gateway IP */
    if (ctx->cfg.ap.ap_ip_addr != 0) {
        struct net_in_addr ip;
        struct net_in_addr nm;

        ip.s_addr = net_htonl(ctx->cfg.ap.ap_ip_addr);
        nm.s_addr = net_htonl(ctx->cfg.ap.ap_subnet_mask);

        struct net_if_addr *ifaddr = net_if_ipv4_addr_add(target_iface, &ip, NET_ADDR_MANUAL, 0);
        if (ifaddr == NULL) {
            LOG_ERR("WiFi: failed to assign static IP to AP interface");
        }
        (void)net_if_ipv4_set_netmask_by_addr(target_iface, &ip, &nm);
        net_if_ipv4_set_gw(target_iface, &ip);

#if defined(CONFIG_NET_DHCPV4_SERVER)
        /* Start DHCPv4 server to dynamically allocate IPs to connected clients */
        struct net_in_addr pool_base;
        /* Pool starts at ap_ip_addr + 1 (e.g., 192.168.4.2) */
        pool_base.s_addr = net_htonl(ctx->cfg.ap.ap_ip_addr + 1U);

        int srv_ret = net_dhcpv4_server_start(target_iface, &pool_base);
        if (srv_ret < 0 && srv_ret != -EALREADY) {
            LOG_ERR("WiFi: failed to start DHCPv4 server (%d)", srv_ret);
        } else {
            LOG_INF("WiFi: DHCPv4 server started (pool base: %u.%u.%u.%u, %d leases)",
                    (unsigned int)((ctx->cfg.ap.ap_ip_addr >> 24) & 0xFF),
                    (unsigned int)((ctx->cfg.ap.ap_ip_addr >> 16) & 0xFF),
                    (unsigned int)((ctx->cfg.ap.ap_ip_addr >> 8) & 0xFF),
                    (unsigned int)(((ctx->cfg.ap.ap_ip_addr) & 0xFF) + 1U),
                    CONFIG_NET_DHCPV4_SERVER_ADDR_COUNT);
        }
#endif
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

#if defined(CONFIG_NET_DHCPV4_SERVER)
    (void)net_dhcpv4_server_stop(target_iface);
    LOG_INF("WiFi: DHCPv4 server stopped on AP");
#endif

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, target_iface, NULL, 0);
    return ret;
}

/* =========================================================================
 * i_network_api_t implementations
 * ========================================================================= */

static int wifi_net_init(void *impl)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL) {
        return -EINVAL;
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.ip_addr = 0;
    ctx->status.subnet_mask = 0;
    ctx->status.gateway = 0;
    ctx->status.rssi = -128;
    ctx->status.link_up_ms = 0;
    ctx->link_up_time = 0;

    if (ctx->sta_iface != NULL) {
        ctx->fsm_state = NET_ADAPTER_STATE_TRANSPORT_READY;
    } else {
        ctx->fsm_state = NET_ADAPTER_STATE_INIT;
    }

    return 0;
}

static int wifi_net_start(void *impl)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL) {
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
        ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
        ctx->status.state = NET_IF_STATE_CONNECTING;
    } else {
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        ctx->status.state = NET_IF_STATE_FAILED;
    }

    return err;
}

static int wifi_net_stop(void *impl)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL) {
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

    ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
    ctx->status.state = NET_IF_STATE_DOWN;
    ctx->status.link_up_ms = 0;
    ctx->link_up_time = 0;

    return 0;
}

static int wifi_net_get_config(void *impl, void *out_data, size_t max_size, size_t *out_size)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || out_data == NULL) {
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

static int wifi_net_set_config(void *impl, const void *data, size_t size)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || data == NULL) {
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

static int wifi_net_get_status(void *impl, net_status_t *out_status)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || out_status == NULL) {
        return -EINVAL;
    }

    struct net_if *active_iface = NULL;
    if (ctx->cfg.mode == NET_WIFI_MODE_AP) {
        active_iface = (ctx->ap_iface != NULL) ? ctx->ap_iface : ctx->sta_iface;
    } else {
        /* In NET_WIFI_MODE_STA and NET_WIFI_MODE_STA_AP (concurrent), prioritize STA interface */
        active_iface = (ctx->sta_iface != NULL) ? ctx->sta_iface : ctx->ap_iface;
    }

    if (active_iface == NULL) {
        ctx->status.state = NET_IF_STATE_FAILED;
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        *out_status = ctx->status;
        return 0;
    }

    if (ctx->fsm_state < NET_ADAPTER_STATE_LINK_UP) {
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
        ctx->fsm_state = NET_ADAPTER_STATE_IP_ACQUIRED;
    } else if (zephyr_status.state == WIFI_STATE_COMPLETED ||
               zephyr_status.state == WIFI_SAP_IFACE_ENABLED) {
        ctx->status.state = NET_IF_STATE_CONNECTING;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
    } else if (zephyr_status.state == WIFI_STATE_DISCONNECTED ||
               zephyr_status.state == WIFI_SAP_IFACE_DISABLED) {
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->status.link_up_ms = 0;
        ctx->link_up_time = 0;
        ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
    } else {
        ctx->status.state = NET_IF_STATE_CONNECTING;
        ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
    }

    *out_status = ctx->status;
    return 0;
}

static const i_network_api_t s_adapter_wifi_esp_hosted_net_api = {
    .init = wifi_net_init,
    .start = wifi_net_start,
    .stop = wifi_net_stop,
    .get_config = wifi_net_get_config,
    .set_config = wifi_net_set_config,
    .get_status = wifi_net_get_status,
};

/* =========================================================================
 * i_wifi_control_api_t implementations
 * ========================================================================= */

static int wifi_ctrl_scan(void *impl)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL) {
        return -EINVAL;
    }

    if (ctx->sta_iface == NULL) {
        return -ENODEV;
    }

    /* Ensure STA interface is admin UP so wifi_mgmt does not reject with -ENETDOWN */
    net_if_up(ctx->sta_iface);
    ctx->scan_count = 0;

    int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, ctx->sta_iface, NULL, 0);
    if (ret != 0) {
        LOG_WRN("WiFi: NET_REQUEST_WIFI_SCAN returned %d", ret);
    }
    return ret;
}

static int wifi_ctrl_connect_ap(void *impl, const char *ssid, const char *psk, net_wifi_security_t sec)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || ssid == NULL || !net_wifi_security_is_valid(sec)) {
        return -EINVAL;
    }

    if (sec != NET_WIFI_SEC_OPEN && psk == NULL) {
        return -EINVAL;
    }

    strncpy(ctx->cfg.sta.ssid, ssid, sizeof(ctx->cfg.sta.ssid) - 1U);
    ctx->cfg.sta.ssid[sizeof(ctx->cfg.sta.ssid) - 1U] = '\0';

    if (psk != NULL) {
        strncpy(ctx->cfg.sta.psk, psk, sizeof(ctx->cfg.sta.psk) - 1U);
        ctx->cfg.sta.psk[sizeof(ctx->cfg.sta.psk) - 1U] = '\0';
    } else {
        ctx->cfg.sta.psk[0] = '\0';
    }

    ctx->cfg.sta.security = (uint8_t)sec;

    /* Promote to concurrent STA_AP if Soft-AP was active, else set STA mode */
    if (ctx->cfg.mode == NET_WIFI_MODE_AP || ctx->cfg.mode == NET_WIFI_MODE_STA_AP) {
        ctx->cfg.mode = (uint8_t)NET_WIFI_MODE_STA_AP;
    } else {
        ctx->cfg.mode = (uint8_t)NET_WIFI_MODE_STA;
    }

    int ret = start_sta(ctx);
    if (ret == 0) {
        ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
        ctx->status.state = NET_IF_STATE_CONNECTING;
    } else {
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        ctx->status.state = NET_IF_STATE_FAILED;
    }

    return ret;
}

static int wifi_ctrl_disconnect(void *impl)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL) {
        return -EINVAL;
    }

    int ret = stop_sta(ctx);
    ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
    ctx->status.state = NET_IF_STATE_DISCONNECTED;

    return ret;
}

static int wifi_ctrl_set_mode(void *impl, net_wifi_mode_t mode)
{
    adapter_wifi_esp_hosted_t *ctx = (adapter_wifi_esp_hosted_t *)impl;

    if (ctx == NULL || !net_wifi_mode_is_valid(mode)) {
        return -EINVAL;
    }

    ctx->cfg.mode = (uint8_t)mode;
    return 0;
}

static const i_wifi_control_api_t s_adapter_wifi_esp_hosted_ctrl_api = {
    .scan = wifi_ctrl_scan,
    .connect_ap = wifi_ctrl_connect_ap,
    .disconnect = wifi_ctrl_disconnect,
    .set_mode = wifi_ctrl_set_mode,
};

#if defined(CONFIG_NET_MGMT_EVENT)
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    adapter_wifi_esp_hosted_t *ctx = CONTAINER_OF(cb, adapter_wifi_esp_hosted_t, wifi_mgmt_cb);
    if (ctx == NULL) {
        return;
    }

    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        if (status != NULL && status->status == 0) {
            LOG_INF("WiFi STA: connected successfully to SSID '%s'", ctx->cfg.sta.ssid);
            ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
            ctx->status.state = NET_IF_STATE_CONNECTING;
            if (ctx->cfg.sta.ip_mode == NET_IP_MODE_DHCP && ctx->sta_iface != NULL) {
                LOG_INF("WiFi STA: starting DHCPv4 client");
                net_dhcpv4_start(ctx->sta_iface);
            }
        } else {
            int code = (status != NULL) ? status->status : -1;
            LOG_WRN("WiFi STA: connection to '%s' failed (status %d)", ctx->cfg.sta.ssid, code);
            ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
            ctx->status.state = NET_IF_STATE_FAILED;
        }
        break;
    }
    case NET_EVENT_WIFI_DISCONNECT_RESULT: {
        LOG_INF("WiFi STA: disconnected from network");
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
        ctx->status.ip_addr = 0;
        ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
        break;
    }
    case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
        LOG_INF("WiFi AP: Soft-AP '%s' enabled", ctx->cfg.ap.ssid);
        break;
    }
    case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
        LOG_INF("WiFi AP: Soft-AP disabled");
        break;
    }
#if defined(CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE)
    case NET_EVENT_WIFI_AP_STA_CONNECTED: {
        const struct wifi_ap_sta_info *sta_info = (const struct wifi_ap_sta_info *)cb->info;
        if (sta_info != NULL) {
            LOG_INF("WiFi Soft-AP: client connected [MAC: %02X:%02X:%02X:%02X:%02X:%02X]",
                    sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
                    sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
        } else {
            LOG_INF("WiFi Soft-AP: client connected");
        }
        break;
    }
    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        const struct wifi_ap_sta_info *sta_info = (const struct wifi_ap_sta_info *)cb->info;
        if (sta_info != NULL) {
            LOG_INF("WiFi Soft-AP: client disconnected [MAC: %02X:%02X:%02X:%02X:%02X:%02X]",
                    sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
                    sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
        } else {
            LOG_INF("WiFi Soft-AP: client disconnected");
        }
        break;
    }
#endif
    case NET_EVENT_WIFI_SCAN_RESULT: {
        const struct wifi_scan_result *res = (const struct wifi_scan_result *)cb->info;
        if (res != NULL && res->ssid_length > 0) {
            char ssid_buf[NET_SSID_MAX_LEN + 1U];
            size_t slen = (res->ssid_length < NET_SSID_MAX_LEN) ? res->ssid_length : NET_SSID_MAX_LEN;
            memcpy(ssid_buf, res->ssid, slen);
            ssid_buf[slen] = '\0';
            LOG_INF("WiFi Scan: AP found '%s' (RSSI: %d dBm, Ch: %u, Sec: %s)",
                    ssid_buf, (int)res->rssi, (unsigned int)res->channel,
                    wifi_security_txt(res->security));

            if (ctx->scan_count < ADAPTER_WIFI_MAX_SCAN_RESULTS) {
                net_wifi_scan_entry_t *entry = &ctx->scan_results[ctx->scan_count++];
                strncpy(entry->ssid, ssid_buf, sizeof(entry->ssid) - 1U);
                entry->ssid[sizeof(entry->ssid) - 1U] = '\0';
                entry->rssi = res->rssi;
                entry->channel = res->channel;
                entry->security = (uint8_t)res->security;
                if (res->mac_length >= NET_MAC_ADDR_LEN) {
                    memcpy(entry->mac, res->mac, NET_MAC_ADDR_LEN);
                } else {
                    memset(entry->mac, 0, sizeof(entry->mac));
                }
            }
        }
        break;
    }
    case NET_EVENT_WIFI_SCAN_DONE: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        int status_val = (status != NULL) ? status->status : 0;
        LOG_INF("WiFi Scan: completed (status=%d, APs found: %u)", status_val, (unsigned int)ctx->scan_count);
        break;
    }
    default:
        break;
    }
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    adapter_wifi_esp_hosted_t *ctx = CONTAINER_OF(cb, adapter_wifi_esp_hosted_t, ipv4_mgmt_cb);
    if (ctx == NULL || iface == NULL) {
        return;
    }

    if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND || mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        struct net_in_addr *addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        if (addr != NULL && addr->s_addr != 0) {
            char ip_str[16];
            net_addr_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
            if (iface == ctx->sta_iface) {
                LOG_INF("WiFi STA: IPv4 address bound: %s", ip_str);
                ctx->status.state = NET_IF_STATE_CONNECTED;
                ctx->status.ip_addr = net_ntohl(addr->s_addr);
                ctx->fsm_state = NET_ADAPTER_STATE_IP_ACQUIRED;
            } else if (iface == ctx->ap_iface) {
                LOG_INF("WiFi AP: IPv4 address configured: %s", ip_str);
            }
        }
    }
}
#endif

int adapter_wifi_esp_hosted_init(adapter_wifi_esp_hosted_t *ctx, const struct device *wifi_dev)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->wifi_dev = wifi_dev;
    ctx->fsm_state = NET_ADAPTER_STATE_INIT;

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
    if (ctx->ap_iface == NULL) {
        LOG_WRN("WiFi: Soft-AP net_if not found at init");
    }
#endif

    LOG_INF("WiFi: interfaces initialized - STA net_if=%p (idx %d), AP net_if=%p (idx %d)",
            (void *)ctx->sta_iface,
            ctx->sta_iface != NULL ? net_if_get_by_iface(ctx->sta_iface) : -1,
            (void *)ctx->ap_iface,
            ctx->ap_iface != NULL ? net_if_get_by_iface(ctx->ap_iface) : -1);

    if (ctx->sta_iface != NULL) {
        struct net_linkaddr *link_addr = net_if_get_link_addr(ctx->sta_iface);
        if (link_addr != NULL && link_addr->len >= NET_MAC_ADDR_LEN) {
            (void)memcpy(ctx->cfg.mac_addr, link_addr->addr, NET_MAC_ADDR_LEN);
        }
        ctx->fsm_state = NET_ADAPTER_STATE_TRANSPORT_READY;
    } else {
        LOG_WRN("WiFi: STA net_if not found at init");
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.rssi = -128;

    if (ctx->sta_iface != NULL) {
        net_if_up(ctx->sta_iface);
    }

#if defined(CONFIG_NET_MGMT_EVENT)
    net_mgmt_init_event_callback(&ctx->wifi_mgmt_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT |
                                 NET_EVENT_WIFI_AP_ENABLE_RESULT |
                                 NET_EVENT_WIFI_AP_DISABLE_RESULT |
#if defined(CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE)
                                 NET_EVENT_WIFI_AP_STA_CONNECTED |
                                 NET_EVENT_WIFI_AP_STA_DISCONNECTED |
#endif
                                 NET_EVENT_WIFI_SCAN_RESULT |
                                 NET_EVENT_WIFI_SCAN_DONE);
    (void)net_mgmt_add_event_callback(&ctx->wifi_mgmt_cb);

    net_mgmt_init_event_callback(&ctx->ipv4_mgmt_cb, ipv4_mgmt_event_handler,
                                 NET_EVENT_IPV4_DHCP_BOUND |
                                 NET_EVENT_IPV4_ADDR_ADD);
    (void)net_mgmt_add_event_callback(&ctx->ipv4_mgmt_cb);
#endif

    return 0;
}

i_network_t adapter_wifi_esp_hosted_port(adapter_wifi_esp_hosted_t *ctx)
{
    i_network_t port = {
        .api = (ctx != NULL && ctx->fsm_state >= NET_ADAPTER_STATE_INIT) ? &s_adapter_wifi_esp_hosted_net_api : NULL,
        .impl = (void *)ctx,
    };
    return port;
}

i_wifi_control_t adapter_wifi_esp_hosted_wifi_control_port(adapter_wifi_esp_hosted_t *ctx)
{
    i_wifi_control_t port = {
        .api = (ctx != NULL && ctx->fsm_state >= NET_ADAPTER_STATE_INIT) ? &s_adapter_wifi_esp_hosted_ctrl_api : NULL,
        .impl = (void *)ctx,
    };
    return port;
}

net_adapter_fsm_state_t adapter_wifi_esp_hosted_get_fsm_state(const adapter_wifi_esp_hosted_t *ctx)
{
    if (ctx == NULL) {
        return NET_ADAPTER_STATE_UNINITIALIZED;
    }
    return ctx->fsm_state;
}

int adapter_wifi_esp_hosted_get_scan_results(adapter_wifi_esp_hosted_t *ctx,
                                             net_wifi_scan_entry_t *out_entries,
                                             size_t max_entries,
                                             size_t *out_count)
{
    if (ctx == NULL || out_entries == NULL) {
        return -EINVAL;
    }

    size_t count = (ctx->scan_count < max_entries) ? ctx->scan_count : max_entries;
    if (count > 0) {
        memcpy(out_entries, ctx->scan_results, count * sizeof(net_wifi_scan_entry_t));
    }
    if (out_count != NULL) {
        *out_count = ctx->scan_count;
    }
    return 0;
}
