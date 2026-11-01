/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet concrete adapter for STM32 RMII LAN8742 PHY behind i_network.
 */

#include "infrastructure/adapters/adapter_eth_stm32.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

LOG_MODULE_REGISTER(adapter_eth_stm32, LOG_LEVEL_INF);

static int apply_ip_config(adapter_eth_stm32_t *ctx)
{
    if (ctx == NULL || ctx->iface == NULL) {
        return -EINVAL;
    }

    if (ctx->cfg.ip_mode == NET_IP_MODE_DHCP) {
#if defined(CONFIG_NET_DHCPV4)
        LOG_INF("Ethernet: starting DHCPv4 client");
        net_dhcpv4_start(ctx->iface);
#else
        LOG_WRN("Ethernet: DHCP requested but CONFIG_NET_DHCPV4 is not enabled");
#endif
    } else if (ctx->cfg.ip_mode == NET_IP_MODE_STATIC) {
#if defined(CONFIG_NET_DHCPV4)
        net_dhcpv4_stop(ctx->iface);
#endif
        struct net_in_addr ip;
        struct net_in_addr nm;
        struct net_in_addr gw;

        ip.s_addr = net_htonl(ctx->cfg.ip_addr);
        nm.s_addr = net_htonl(ctx->cfg.subnet_mask);
        gw.s_addr = net_htonl(ctx->cfg.gateway);

        LOG_INF("Ethernet: applying static IPv4: 0x%08X", (unsigned int)ctx->cfg.ip_addr);

        /* Add or replace static unicast address */
        struct net_if_addr *ifaddr = net_if_ipv4_addr_add(ctx->iface, &ip, NET_ADDR_MANUAL, 0);
        if (ifaddr == NULL) {
            LOG_WRN("Ethernet: net_if_ipv4_addr_add returned NULL (address may already exist)");
        }

        if (!net_if_ipv4_set_netmask_by_addr(ctx->iface, &ip, &nm)) {
            LOG_WRN("Ethernet: failed to set netmask");
        }

        net_if_ipv4_set_gw(ctx->iface, &gw);
    }

    return 0;
}

static int eth_api_init(void *impl, network_interface_t iface)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET) {
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

static int eth_api_start(void *impl, network_interface_t iface)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        return -ENODEV;
    }

    int err = net_if_up(ctx->iface);
    if (err != 0 && err != -EALREADY) {
        LOG_ERR("Ethernet: net_if_up failed (%d)", err);
        ctx->status.state = NET_IF_STATE_FAILED;
        return err;
    }

    ctx->is_started = true;
    (void)apply_ip_config(ctx);

    if (net_if_is_carrier_ok(ctx->iface)) {
        ctx->status.state = NET_IF_STATE_CONNECTING;
    } else {
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
    }

    return 0;
}

static int eth_api_stop(void *impl, network_interface_t iface)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        return -ENODEV;
    }

#if defined(CONFIG_NET_DHCPV4)
    if (ctx->cfg.ip_mode == NET_IP_MODE_DHCP) {
        net_dhcpv4_stop(ctx->iface);
    }
#endif

    int err = net_if_down(ctx->iface);
    ctx->is_started = false;
    ctx->status.state = NET_IF_STATE_DOWN;
    ctx->link_up_time = 0;
    ctx->status.link_up_ms = 0;

    return err;
}

static int eth_api_get_config(void *impl, network_interface_t iface,
                             void *out_data, size_t max_size, size_t *out_size)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET || out_data == NULL) {
        return -EINVAL;
    }

    if (max_size < sizeof(net_eth_config_t)) {
        return -ENOMEM;
    }

    (void)memcpy(out_data, &ctx->cfg, sizeof(net_eth_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_eth_config_t);
    }

    return 0;
}

static int eth_api_set_config(void *impl, network_interface_t iface,
                             const void *data, size_t size)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET || data == NULL) {
        return -EINVAL;
    }

    if (size != sizeof(net_eth_config_t)) {
        return -EINVAL;
    }

    const net_eth_config_t *new_cfg = (const net_eth_config_t *)data;
    if (!net_eth_config_is_valid(new_cfg)) {
        return -EINVAL;
    }

    uint8_t saved_mac[NET_MAC_ADDR_LEN];
    (void)memcpy(saved_mac, ctx->cfg.mac_addr, sizeof(saved_mac));

    (void)memcpy(&ctx->cfg, new_cfg, sizeof(net_eth_config_t));
    (void)memcpy(ctx->cfg.mac_addr, saved_mac, sizeof(ctx->cfg.mac_addr));

    if (ctx->is_started && ctx->iface != NULL) {
        (void)apply_ip_config(ctx);
    }

    return 0;
}

static int eth_api_get_status(void *impl, network_interface_t iface,
                             net_status_t *out_status)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || iface != NETWORK_INTERFACE_ETHERNET || out_status == NULL) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        ctx->status.state = NET_IF_STATE_FAILED;
        *out_status = ctx->status;
        return 0;
    }

    if (!ctx->is_started || !net_if_is_up(ctx->iface)) {
        ctx->status.state = NET_IF_STATE_DOWN;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->status.link_up_ms = 0;
        ctx->link_up_time = 0;
        *out_status = ctx->status;
        return 0;
    }

    bool carrier_ok = net_if_is_carrier_ok(ctx->iface);
    uint32_t now = (uint32_t)k_uptime_get();

    if (!carrier_ok) {
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->link_up_time = 0;
        ctx->status.link_up_ms = 0;
    } else {
        if (ctx->link_up_time == 0) {
            ctx->link_up_time = now;
        }
        ctx->status.link_up_ms = now - ctx->link_up_time;

        struct net_in_addr *addr = net_if_ipv4_get_global_addr(ctx->iface, NET_ADDR_PREFERRED);
        if (addr != NULL && addr->s_addr != 0) {
            ctx->status.state = NET_IF_STATE_CONNECTED;
            ctx->status.ip_addr = net_ntohl(addr->s_addr);

            struct net_in_addr nm = net_if_ipv4_get_netmask_by_addr(ctx->iface, addr);
            ctx->status.subnet_mask = net_ntohl(nm.s_addr);

            struct net_in_addr gw = net_if_ipv4_get_gw(ctx->iface);
            ctx->status.gateway = net_ntohl(gw.s_addr);
        } else {
            ctx->status.state = NET_IF_STATE_CONNECTING;
            ctx->status.ip_addr = 0;
            ctx->status.subnet_mask = 0;
            ctx->status.gateway = 0;
        }
    }

    ctx->status.rssi = -128;
    *out_status = ctx->status;
    return 0;
}

static int eth_api_scan(void *impl, network_interface_t iface)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(iface);
    return -ENOSYS;
}

static const i_network_api_t s_adapter_eth_stm32_api = {
    .init = eth_api_init,
    .start = eth_api_start,
    .stop = eth_api_stop,
    .get_config = eth_api_get_config,
    .set_config = eth_api_set_config,
    .get_status = eth_api_get_status,
    .scan = eth_api_scan,
};

int adapter_eth_stm32_init(adapter_eth_stm32_t *ctx, const struct device *eth_dev)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->eth_dev = eth_dev;

    (void)network_default_eth_config(&ctx->cfg);

    if (eth_dev != NULL && device_is_ready(eth_dev)) {
        ctx->iface = net_if_lookup_by_dev(eth_dev);
    }

    if (ctx->iface == NULL) {
#if defined(CONFIG_NET_L2_ETHERNET)
        ctx->iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
#endif
    }

    if (ctx->iface != NULL) {
        struct net_linkaddr *link_addr = net_if_get_link_addr(ctx->iface);
        if (link_addr != NULL && link_addr->len >= NET_MAC_ADDR_LEN) {
            (void)memcpy(ctx->cfg.mac_addr, link_addr->addr, NET_MAC_ADDR_LEN);
        }
    } else {
        LOG_WRN("Ethernet: net_if not found at init");
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.rssi = -128;
    ctx->is_ready = true;
    ctx->is_started = false;

    return 0;
}

i_network_t adapter_eth_stm32_port(adapter_eth_stm32_t *ctx)
{
    i_network_t port = {
        .api = (ctx != NULL && ctx->is_ready) ? &s_adapter_eth_stm32_api : NULL,
        .impl = (void *)ctx,
    };
    return port;
}
