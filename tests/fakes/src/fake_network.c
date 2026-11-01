/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake network port implementation.
 */

#include "fake_network.h"
#include <errno.h>
#include <string.h>

static fake_network_state_t s_fake_net;

/* Ethernet Adapter Implementation */
static int fake_eth_init(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_init_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    s->eth_status.state = NET_IF_STATE_DISCONNECTED;
    return 0;
}

static int fake_eth_start(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_start_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    s->eth_status.state = NET_IF_STATE_CONNECTING;
    return 0;
}

static int fake_eth_stop(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_stop_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    s->eth_status.state = NET_IF_STATE_DOWN;
    return 0;
}

static int fake_eth_get_config(void *impl, network_interface_t iface,
                               void *out_data, size_t max_size, size_t *out_size)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_get_config_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    if (max_size < sizeof(net_eth_config_t)) {
        return -ENOSPC;
    }
    memcpy(out_data, &s->eth_config, sizeof(net_eth_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_eth_config_t);
    }
    return 0;
}

static int fake_eth_set_config(void *impl, network_interface_t iface,
                               const void *data, size_t size)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_set_config_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    if (size != sizeof(net_eth_config_t)) {
        return -EINVAL;
    }
    memcpy(&s->eth_config, data, sizeof(net_eth_config_t));
    return 0;
}

static int fake_eth_get_status(void *impl, network_interface_t iface,
                               net_status_t *out_status)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_get_status_calls++;
    if (s->eth_return_code != 0) {
        return s->eth_return_code;
    }
    memcpy(out_status, &s->eth_status, sizeof(net_status_t));
    return 0;
}

static int fake_eth_scan(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->eth_scan_calls++;
    /* Ethernet does not support scanning */
    return -ENOSYS;
}

static const i_network_api_t s_fake_eth_api = {
    .init = fake_eth_init,
    .start = fake_eth_start,
    .stop = fake_eth_stop,
    .get_config = fake_eth_get_config,
    .set_config = fake_eth_set_config,
    .get_status = fake_eth_get_status,
    .scan = fake_eth_scan,
};

/* WiFi Adapter Implementation */
static int fake_wifi_init(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_init_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    s->wifi_status.state = NET_IF_STATE_DISCONNECTED;
    return 0;
}

static int fake_wifi_start(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_start_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    s->wifi_status.state = NET_IF_STATE_CONNECTING;
    return 0;
}

static int fake_wifi_stop(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_stop_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    s->wifi_status.state = NET_IF_STATE_DOWN;
    return 0;
}

static int fake_wifi_get_config(void *impl, network_interface_t iface,
                                void *out_data, size_t max_size, size_t *out_size)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_get_config_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    if (max_size < sizeof(net_wifi_config_t)) {
        return -ENOSPC;
    }
    memcpy(out_data, &s->wifi_config, sizeof(net_wifi_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_wifi_config_t);
    }
    return 0;
}

static int fake_wifi_set_config(void *impl, network_interface_t iface,
                                const void *data, size_t size)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_set_config_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    if (size != sizeof(net_wifi_config_t)) {
        return -EINVAL;
    }
    memcpy(&s->wifi_config, data, sizeof(net_wifi_config_t));
    return 0;
}

static int fake_wifi_get_status(void *impl, network_interface_t iface,
                                net_status_t *out_status)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_get_status_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    memcpy(out_status, &s->wifi_status, sizeof(net_status_t));
    return 0;
}

static int fake_wifi_scan(void *impl, network_interface_t iface)
{
    fake_network_state_t *s = (fake_network_state_t *)impl;
    s->wifi_scan_calls++;
    if (s->wifi_return_code != 0) {
        return s->wifi_return_code;
    }
    return 0;
}

static const i_network_api_t s_fake_wifi_api = {
    .init = fake_wifi_init,
    .start = fake_wifi_start,
    .stop = fake_wifi_stop,
    .get_config = fake_wifi_get_config,
    .set_config = fake_wifi_set_config,
    .get_status = fake_wifi_get_status,
    .scan = fake_wifi_scan,
};

void fake_network_reset(void)
{
    memset(&s_fake_net, 0, sizeof(s_fake_net));
    s_fake_net.eth_status.state = NET_IF_STATE_DOWN;
    s_fake_net.eth_status.rssi = -128;
    s_fake_net.wifi_status.state = NET_IF_STATE_DOWN;
    s_fake_net.wifi_status.rssi = -128;
}

i_network_t fake_network_get_eth_port(void)
{
    i_network_t port = {
        .api = &s_fake_eth_api,
        .impl = &s_fake_net,
    };
    return port;
}

i_network_t fake_network_get_wifi_port(void)
{
    i_network_t port = {
        .api = &s_fake_wifi_api,
        .impl = &s_fake_net,
    };
    return port;
}

const fake_network_state_t *fake_network_get_state(void)
{
    return &s_fake_net;
}

void fake_network_set_return_code(network_interface_t iface, int ret_code)
{
    if (iface == NETWORK_INTERFACE_ETHERNET) {
        s_fake_net.eth_return_code = ret_code;
    } else if (iface == NETWORK_INTERFACE_WIFI) {
        s_fake_net.wifi_return_code = ret_code;
    }
}

void fake_network_set_state(network_interface_t iface,
                            net_if_state_t state,
                            uint32_t ip,
                            uint32_t mask,
                            uint32_t gw)
{
    net_status_t *status = (iface == NETWORK_INTERFACE_ETHERNET)
                           ? &s_fake_net.eth_status
                           : &s_fake_net.wifi_status;

    status->state = state;
    status->ip_addr = ip;
    status->subnet_mask = mask;
    status->gateway = gw;
}

void fake_network_set_wifi_rssi(int8_t rssi)
{
    s_fake_net.wifi_status.rssi = rssi;
}
