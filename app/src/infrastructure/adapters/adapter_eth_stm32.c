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
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

LOG_MODULE_REGISTER(adapter_eth_stm32, LOG_LEVEL_INF);

struct remove_ipv4_ctx {
    struct net_in_addr addrs[4];
    size_t count;
};

static void collect_unicast_ipv4(struct net_if *iface, struct net_if_addr *ifaddr, void *user_data)
{
    ARG_UNUSED(iface);
    struct remove_ipv4_ctx *c = (struct remove_ipv4_ctx *)user_data;
    if (ifaddr != NULL && ifaddr->is_used && c->count < ARRAY_SIZE(c->addrs)) {
        c->addrs[c->count++] = ifaddr->address.in_addr;
    }
}

static void clear_iface_ipv4_addresses(struct net_if *iface)
{
    if (iface == NULL) {
        return;
    }
    struct remove_ipv4_ctx coll = { 0 };
    net_if_ipv4_addr_foreach(iface, collect_unicast_ipv4, &coll);
    for (size_t i = 0; i < coll.count; i++) {
        (void)net_if_ipv4_addr_rm(iface, &coll.addrs[i]);
    }
}

static int apply_ip_config(adapter_eth_stm32_t *ctx)
{
    if (ctx == NULL || ctx->iface == NULL) {
        return -EINVAL;
    }

    if (ctx->cfg.ip_mode == NET_IP_MODE_DHCP) {
#if defined(CONFIG_NET_DHCPV4)
        clear_iface_ipv4_addresses(ctx->iface);
        LOG_INF("Ethernet: starting DHCPv4 client");
        net_dhcpv4_start(ctx->iface);
#else
        LOG_WRN("Ethernet: DHCP requested but CONFIG_NET_DHCPV4 is not enabled");
#endif
    } else if (ctx->cfg.ip_mode == NET_IP_MODE_STATIC) {
#if defined(CONFIG_NET_DHCPV4)
        net_dhcpv4_stop(ctx->iface);
#endif
        clear_iface_ipv4_addresses(ctx->iface);
        struct net_in_addr ip;
        struct net_in_addr nm;
        struct net_in_addr gw;

        ip.s_addr = net_htonl(ctx->cfg.ip_addr);
        nm.s_addr = net_htonl(ctx->cfg.subnet_mask);
        gw.s_addr = net_htonl(ctx->cfg.gateway);

        LOG_INF("Ethernet: applying static IPv4: 0x%08X", (unsigned int)ctx->cfg.ip_addr);

        /* Add static unicast address */
        struct net_if_addr *ifaddr = net_if_ipv4_addr_add(ctx->iface, &ip, NET_ADDR_MANUAL, 0);
        if (ifaddr == NULL) {
            LOG_WRN("Ethernet: net_if_ipv4_addr_add returned NULL");
        }

        if (!net_if_ipv4_set_netmask_by_addr(ctx->iface, &ip, &nm)) {
            LOG_WRN("Ethernet: failed to set netmask");
        }

        net_if_ipv4_set_gw(ctx->iface, &gw);
    }

    return 0;
}

static int eth_api_init(void *impl)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

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

    if (ctx->iface != NULL) {
        ctx->fsm_state = NET_ADAPTER_STATE_TRANSPORT_READY;
    } else {
        ctx->fsm_state = NET_ADAPTER_STATE_INIT;
    }

    return 0;
}

static int eth_api_start(void *impl)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        return -ENODEV;
    }

    int err = net_if_up(ctx->iface);
    if (err != 0 && err != -EALREADY) {
        LOG_ERR("Ethernet: net_if_up failed (%d)", err);
        ctx->status.state = NET_IF_STATE_FAILED;
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        return err;
    }

    (void)apply_ip_config(ctx);

    if (net_if_is_carrier_ok(ctx->iface)) {
        ctx->status.state = NET_IF_STATE_CONNECTING;
        ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
    } else {
        ctx->status.state = NET_IF_STATE_DISCONNECTED;
        ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
    }

    return 0;
}

static int eth_api_stop(void *impl)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        return -ENODEV;
    }

#if defined(CONFIG_NET_DHCPV4)
    if (ctx->cfg.ip_mode == NET_IP_MODE_DHCP) {
        net_dhcpv4_stop(ctx->iface);
    }
#endif

    int err = net_if_down(ctx->iface);
    ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
    ctx->status.state = NET_IF_STATE_DOWN;
    ctx->link_up_time = 0;
    ctx->status.link_up_ms = 0;

    return err;
}

static int eth_api_get_config(void *impl, void *out_data, size_t max_size, size_t *out_size)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || out_data == NULL) {
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

static int eth_api_set_config(void *impl, const void *data, size_t size)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || data == NULL) {
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

    if (ctx->fsm_state >= NET_ADAPTER_STATE_LINK_UP && ctx->iface != NULL) {
        (void)apply_ip_config(ctx);
    }

    return 0;
}

static int eth_api_get_status(void *impl, net_status_t *out_status)
{
    adapter_eth_stm32_t *ctx = (adapter_eth_stm32_t *)impl;

    if (ctx == NULL || out_status == NULL) {
        return -EINVAL;
    }

    if (ctx->iface == NULL) {
        ctx->status.state = NET_IF_STATE_FAILED;
        ctx->fsm_state = NET_ADAPTER_STATE_ERROR;
        *out_status = ctx->status;
        return 0;
    }

    if (!net_if_is_up(ctx->iface)) {
        ctx->status.state = NET_IF_STATE_DOWN;
        ctx->status.ip_addr = 0;
        ctx->status.subnet_mask = 0;
        ctx->status.gateway = 0;
        ctx->status.link_up_ms = 0;
        ctx->link_up_time = 0;
        ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
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
        ctx->fsm_state = NET_ADAPTER_STATE_DISCONNECTED;
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

            ctx->fsm_state = NET_ADAPTER_STATE_IP_ACQUIRED;
        } else {
            ctx->status.state = NET_IF_STATE_CONNECTING;
            ctx->status.ip_addr = 0;
            ctx->status.subnet_mask = 0;
            ctx->status.gateway = 0;
            ctx->fsm_state = NET_ADAPTER_STATE_LINK_UP;
        }
    }

    ctx->status.rssi = -128;
    *out_status = ctx->status;
    return 0;
}

static const i_network_api_t s_adapter_eth_stm32_api = {
    .init = eth_api_init,
    .start = eth_api_start,
    .stop = eth_api_stop,
    .get_config = eth_api_get_config,
    .set_config = eth_api_set_config,
    .get_status = eth_api_get_status,
};

#if defined(CONFIG_NET_MGMT_EVENT)
static void eth_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event,
                                   struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);
    if (mgmt_event == NET_EVENT_ETHERNET_CARRIER_ON) {
        LOG_INF("Ethernet: Link UP (Carrier detected)");
    } else if (mgmt_event == NET_EVENT_ETHERNET_CARRIER_OFF) {
        LOG_INF("Ethernet: Link DOWN (Cable disconnected)");
    }
}

static void eth_ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                        uint64_t mgmt_event,
                                        struct net_if *iface)
{
    adapter_eth_stm32_t *ctx = CONTAINER_OF(cb, adapter_eth_stm32_t, ipv4_mgmt_cb);
    if (ctx == NULL || iface == NULL || iface != ctx->iface) {
        return;
    }

    if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND || mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        struct net_in_addr *addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        if (addr != NULL && addr->s_addr != 0) {
            char ip_str[16];
            net_addr_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
            LOG_INF("Ethernet: IPv4 address bound: %s", ip_str);
            ctx->status.state = NET_IF_STATE_CONNECTED;
            ctx->status.ip_addr = net_ntohl(addr->s_addr);
            ctx->fsm_state = NET_ADAPTER_STATE_IP_ACQUIRED;
        }
    }
}
#endif

int adapter_eth_stm32_init(adapter_eth_stm32_t *ctx, const struct device *eth_dev)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->eth_dev = eth_dev;
    ctx->fsm_state = NET_ADAPTER_STATE_INIT;

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
        ctx->fsm_state = NET_ADAPTER_STATE_TRANSPORT_READY;
    } else {
        LOG_WRN("Ethernet: net_if not found at init");
    }

    ctx->status.state = NET_IF_STATE_DISCONNECTED;
    ctx->status.rssi = -128;

#if defined(CONFIG_NET_MGMT_EVENT)
    net_mgmt_init_event_callback(&ctx->eth_mgmt_cb, eth_mgmt_event_handler,
                                 NET_EVENT_ETHERNET_CARRIER_ON |
                                 NET_EVENT_ETHERNET_CARRIER_OFF);
    (void)net_mgmt_add_event_callback(&ctx->eth_mgmt_cb);

    net_mgmt_init_event_callback(&ctx->ipv4_mgmt_cb, eth_ipv4_mgmt_event_handler,
                                 NET_EVENT_IPV4_DHCP_BOUND |
                                 NET_EVENT_IPV4_ADDR_ADD);
    (void)net_mgmt_add_event_callback(&ctx->ipv4_mgmt_cb);
#endif

    return 0;
}

i_network_t adapter_eth_stm32_port(adapter_eth_stm32_t *ctx)
{
    i_network_t port = {
        .api = (ctx != NULL && ctx->fsm_state >= NET_ADAPTER_STATE_INIT) ? &s_adapter_eth_stm32_api : NULL,
        .impl = (void *)ctx,
    };
    return port;
}

net_adapter_fsm_state_t adapter_eth_stm32_get_fsm_state(const adapter_eth_stm32_t *ctx)
{
    if (ctx == NULL) {
        return NET_ADAPTER_STATE_UNINITIALIZED;
    }
    return ctx->fsm_state;
}
