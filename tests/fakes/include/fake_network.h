/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake network port implementation for unit testing.
 * Provides independent Ethernet and WiFi simulation context,
 * call counting, error injection, state simulation, and config storage.
 */

#ifndef FAKE_NETWORK_H_
#define FAKE_NETWORK_H_

#include "interfaces/i_network.h"
#include "interfaces/i_wifi_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Call counters - Ethernet i_network */
    uint32_t eth_init_calls;
    uint32_t eth_start_calls;
    uint32_t eth_stop_calls;
    uint32_t eth_get_config_calls;
    uint32_t eth_set_config_calls;
    uint32_t eth_get_status_calls;

    /* Call counters - WiFi i_network */
    uint32_t wifi_init_calls;
    uint32_t wifi_start_calls;
    uint32_t wifi_stop_calls;
    uint32_t wifi_get_config_calls;
    uint32_t wifi_set_config_calls;
    uint32_t wifi_get_status_calls;

    /* Call counters - WiFi i_wifi_control */
    uint32_t wifi_scan_calls;
    uint32_t wifi_connect_ap_calls;
    uint32_t wifi_disconnect_calls;
    uint32_t wifi_set_mode_calls;

    /* Recorded parameters from wifi control calls */
    char                last_connect_ssid[NET_SSID_MAX_LEN + 1U];
    char                last_connect_psk[NET_PSK_MAX_LEN + 1U];
    net_wifi_security_t last_connect_sec;
    net_wifi_mode_t     last_mode;

    /* Injected return codes (0 for normal behavior) */
    int eth_return_code;
    int wifi_return_code;
    int wifi_ctrl_return_code;

    /* Simulated configuration and status */
    net_eth_config_t  eth_config;
    net_wifi_config_t wifi_config;
    net_status_t      eth_status;
    net_status_t      wifi_status;
} fake_network_state_t;

/**
 * @brief Reset the fake network state and clear all counters.
 */
void fake_network_reset(void);

/**
 * @brief Obtain an i_network_t port handle bound to the fake Ethernet adapter.
 */
i_network_t fake_network_get_eth_port(void);

/**
 * @brief Obtain an i_network_t port handle bound to the fake WiFi adapter.
 */
i_network_t fake_network_get_wifi_port(void);

/**
 * @brief Obtain an i_wifi_control_t port handle bound to the fake WiFi adapter.
 */
i_wifi_control_t fake_network_get_wifi_control_port(void);

/**
 * @brief Direct inspection of the internal fake state.
 */
const fake_network_state_t *fake_network_get_state(void);

/**
 * @brief Inject an error return code for subsequent operations on given interface.
 */
void fake_network_set_return_code(network_interface_t iface, int ret_code);

/**
 * @brief Inject an error return code for subsequent WiFi control operations.
 */
void fake_network_set_wifi_ctrl_return_code(int ret_code);

/**
 * @brief Simulate state change (e.g. connected, IP assigned) for a given interface.
 */
void fake_network_set_state(network_interface_t iface,
                            net_if_state_t state,
                            uint32_t ip,
                            uint32_t mask,
                            uint32_t gw);

/**
 * @brief Set RSSI value for WiFi simulation.
 */
void fake_network_set_wifi_rssi(int8_t rssi);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_NETWORK_H_ */
