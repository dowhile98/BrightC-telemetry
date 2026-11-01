/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for network types, validators, abstract port wrappers,
 * and in-memory fake network implementation.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "common/network_types.h"
#include "interfaces/i_network.h"
#include "fake_network.h"
#include "application/zbus/network_channels.h"
#include "application/active_objects/ao_network.h"

ZTEST_SUITE(network_suite, NULL, NULL, NULL, NULL, NULL);


ZTEST(network_suite, test_network_interface_enum_and_validation)
{
    zassert_equal(NETWORK_INTERFACE_ETHERNET, 0, "Ethernet interface must be 0");
    zassert_equal(NETWORK_INTERFACE_WIFI, 1, "WiFi interface must be 1");
    zassert_equal(NETWORK_INTERFACE_MAX, 2, "Max network interfaces must be 2");

    zassert_true(network_interface_is_valid(NETWORK_INTERFACE_ETHERNET), "Ethernet must be valid");
    zassert_true(network_interface_is_valid(NETWORK_INTERFACE_WIFI), "WiFi must be valid");
    zassert_false(network_interface_is_valid(NETWORK_INTERFACE_MAX), "MAX must be invalid");
    zassert_false(network_interface_is_valid((network_interface_t)99), "99 must be invalid");
}

ZTEST(network_suite, test_net_if_state_enum_and_validation)
{
    zassert_equal(NET_IF_STATE_DOWN, 0, "DOWN must be 0");
    zassert_equal(NET_IF_STATE_DISCONNECTED, 1, "DISCONNECTED must be 1");
    zassert_equal(NET_IF_STATE_CONNECTING, 2, "CONNECTING must be 2");
    zassert_equal(NET_IF_STATE_CONNECTED, 3, "CONNECTED must be 3");
    zassert_equal(NET_IF_STATE_FAILED, 4, "FAILED must be 4");

    zassert_true(net_if_state_is_valid(NET_IF_STATE_DOWN), "DOWN valid");
    zassert_true(net_if_state_is_valid(NET_IF_STATE_DISCONNECTED), "DISCONNECTED valid");
    zassert_true(net_if_state_is_valid(NET_IF_STATE_CONNECTING), "CONNECTING valid");
    zassert_true(net_if_state_is_valid(NET_IF_STATE_CONNECTED), "CONNECTED valid");
    zassert_true(net_if_state_is_valid(NET_IF_STATE_FAILED), "FAILED valid");
    zassert_false(net_if_state_is_valid((net_if_state_t)5), "State 5 invalid");
}

ZTEST(network_suite, test_net_ip_mode_enum_and_validation)
{
    zassert_equal(NET_IP_MODE_DHCP, 0, "DHCP must be 0");
    zassert_equal(NET_IP_MODE_STATIC, 1, "STATIC must be 1");

    zassert_true(net_ip_mode_is_valid(NET_IP_MODE_DHCP), "DHCP valid");
    zassert_true(net_ip_mode_is_valid(NET_IP_MODE_STATIC), "STATIC valid");
    zassert_false(net_ip_mode_is_valid((net_ip_mode_t)2), "Mode 2 invalid");
}

ZTEST(network_suite, test_net_wifi_mode_enum_and_validation)
{
    zassert_equal(NET_WIFI_MODE_STA, 0, "STA must be 0");
    zassert_equal(NET_WIFI_MODE_AP, 1, "AP must be 1");
    zassert_equal(NET_WIFI_MODE_STA_AP, 2, "STA_AP must be 2");

    zassert_true(net_wifi_mode_is_valid(NET_WIFI_MODE_STA), "STA valid");
    zassert_true(net_wifi_mode_is_valid(NET_WIFI_MODE_AP), "AP valid");
    zassert_true(net_wifi_mode_is_valid(NET_WIFI_MODE_STA_AP), "STA_AP valid");
    zassert_false(net_wifi_mode_is_valid((net_wifi_mode_t)3), "Mode 3 invalid");
}

ZTEST(network_suite, test_net_wifi_security_enum_and_validation)
{
    zassert_equal(NET_WIFI_SEC_OPEN, 0, "OPEN must be 0");
    zassert_equal(NET_WIFI_SEC_WPA_PSK, 2, "WPA_PSK must be 2");
    zassert_equal(NET_WIFI_SEC_WPA2_PSK, 3, "WPA2_PSK must be 3");
    zassert_equal(NET_WIFI_SEC_WPA3_SAE, 6, "WPA3_SAE must be 6");

    zassert_true(net_wifi_security_is_valid(NET_WIFI_SEC_OPEN), "OPEN valid");
    zassert_true(net_wifi_security_is_valid(NET_WIFI_SEC_WPA_PSK), "WPA_PSK valid");
    zassert_true(net_wifi_security_is_valid(NET_WIFI_SEC_WPA2_PSK), "WPA2_PSK valid");
    zassert_true(net_wifi_security_is_valid(NET_WIFI_SEC_WPA3_SAE), "WPA3_SAE valid");
    zassert_false(net_wifi_security_is_valid((net_wifi_security_t)1), "Security 1 invalid");
    zassert_false(net_wifi_security_is_valid((net_wifi_security_t)4), "Security 4 invalid");
}

ZTEST(network_suite, test_net_service_bind_mode_enum_and_validation)
{
    zassert_equal(NET_BIND_ANY, 0, "ANY must be 0");
    zassert_equal(NET_BIND_ETHERNET_ONLY, 1, "ETH only must be 1");
    zassert_equal(NET_BIND_WIFI_ONLY, 2, "WiFi only must be 2");

    zassert_true(net_service_bind_mode_is_valid(NET_BIND_ANY), "ANY valid");
    zassert_true(net_service_bind_mode_is_valid(NET_BIND_ETHERNET_ONLY), "ETH valid");
    zassert_true(net_service_bind_mode_is_valid(NET_BIND_WIFI_ONLY), "WiFi valid");
    zassert_false(net_service_bind_mode_is_valid((net_service_bind_mode_t)3), "Mode 3 invalid");
}

ZTEST(network_suite, test_net_result_enum_values)
{
    zassert_equal(NETWORK_RESULT_OK, 0, "OK is 0");
    zassert_equal(NETWORK_RESULT_INVALID, -1, "INVALID is -1");
    zassert_equal(NETWORK_RESULT_NOT_READY, -2, "NOT_READY is -2");
    zassert_equal(NETWORK_RESULT_NOT_CONNECTED, -3, "NOT_CONNECTED is -3");
    zassert_equal(NETWORK_RESULT_TIMEOUT, -4, "TIMEOUT is -4");
    zassert_equal(NETWORK_RESULT_IO_ERROR, -5, "IO_ERROR is -5");
    zassert_equal(NETWORK_RESULT_UNSUPPORTED, -6, "UNSUPPORTED is -6");
}

ZTEST(network_suite, test_net_eth_config_validators)
{
    net_eth_config_t cfg = {
        .ip_mode = NET_IP_MODE_DHCP,
        .mac_addr = {0x00, 0x80, 0xE1, 0x01, 0x02, 0x03},
        .ip_addr = 0,
        .subnet_mask = 0,
        .gateway = 0,
        .auto_dhcpv4 = true,
    };

    zassert_true(net_eth_config_is_valid(&cfg), "Valid DHCP eth config");
    zassert_false(net_eth_config_is_valid(NULL), "NULL config invalid");

    cfg.ip_mode = (net_ip_mode_t)99;
    zassert_false(net_eth_config_is_valid(&cfg), "Invalid ip_mode rejected");

    cfg.ip_mode = NET_IP_MODE_STATIC;
    cfg.ip_addr = 0xC0A80164;      /* 192.168.1.100 */
    cfg.subnet_mask = 0xFFFFFF00;  /* 255.255.255.0 */
    cfg.gateway = 0xC0A80101;      /* 192.168.1.1 */
    zassert_true(net_eth_config_is_valid(&cfg), "Valid static eth config");

    cfg.ip_addr = 0;
    zassert_false(net_eth_config_is_valid(&cfg), "Static eth with IP 0 invalid");
}

ZTEST(network_suite, test_net_wifi_config_validators)
{
    net_wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = NET_WIFI_MODE_AP;
    strncpy(cfg.ap.ssid, "BrightC-AP", sizeof(cfg.ap.ssid) - 1);
    cfg.ap.security = NET_WIFI_SEC_OPEN;
    cfg.ap.channel = 6;
    cfg.ap.max_clients = 4;
    cfg.ap.ap_ip_addr = 0xC0A80401; /* 192.168.4.1 */
    cfg.ap.ap_subnet_mask = 0xFFFFFF00;

    zassert_true(net_wifi_config_is_valid(&cfg), "Valid AP config");
    zassert_false(net_wifi_config_is_valid(NULL), "NULL wifi config invalid");

    /* Channel must be 1..14 */
    cfg.ap.channel = 0;
    zassert_false(net_wifi_config_is_valid(&cfg), "Channel 0 invalid");
    cfg.ap.channel = 15;
    zassert_false(net_wifi_config_is_valid(&cfg), "Channel 15 invalid");
    cfg.ap.channel = 6;

    /* Empty SSID */
    cfg.ap.ssid[0] = '\0';
    zassert_false(net_wifi_config_is_valid(&cfg), "Empty AP SSID invalid");
    strncpy(cfg.ap.ssid, "BrightC-AP", sizeof(cfg.ap.ssid) - 1);

    /* Test STA mode */
    cfg.mode = NET_WIFI_MODE_STA;
    strncpy(cfg.sta.ssid, "MyHomeWiFi", sizeof(cfg.sta.ssid) - 1);
    strncpy(cfg.sta.psk, "Password123", sizeof(cfg.sta.psk) - 1);
    cfg.sta.security = NET_WIFI_SEC_WPA2_PSK;
    cfg.sta.ip_mode = NET_IP_MODE_DHCP;
    zassert_true(net_wifi_config_is_valid(&cfg), "Valid STA config");

    /* WPA2 with password < 8 chars is invalid */
    strncpy(cfg.sta.psk, "short", sizeof(cfg.sta.psk) - 1);
    zassert_false(net_wifi_config_is_valid(&cfg), "Password < 8 chars invalid for WPA2");
}

ZTEST(network_suite, test_i_network_wrappers_unbound_and_null)
{
    i_network_t unbound = {0};
    net_status_t status;
    net_eth_config_t cfg;
    size_t out_size = 0;

    zassert_false(i_network_is_bound(NULL), "NULL handle unbound");
    zassert_false(i_network_is_bound(&unbound), "Zero handle unbound");

    /* All wrappers return -EINVAL on unbound handle */
    zassert_equal(i_network_init(&unbound, NETWORK_INTERFACE_ETHERNET), -EINVAL, "Unbound init");
    zassert_equal(i_network_start(&unbound, NETWORK_INTERFACE_ETHERNET), -EINVAL, "Unbound start");
    zassert_equal(i_network_stop(&unbound, NETWORK_INTERFACE_ETHERNET), -EINVAL, "Unbound stop");
    zassert_equal(i_network_get_config(&unbound, NETWORK_INTERFACE_ETHERNET, &cfg, sizeof(cfg), &out_size), -EINVAL, "Unbound get_config");
    zassert_equal(i_network_set_config(&unbound, NETWORK_INTERFACE_ETHERNET, &cfg, sizeof(cfg)), -EINVAL, "Unbound set_config");
    zassert_equal(i_network_get_status(&unbound, NETWORK_INTERFACE_ETHERNET, &status), -EINVAL, "Unbound get_status");
    zassert_equal(i_network_scan(&unbound, NETWORK_INTERFACE_WIFI), -EINVAL, "Unbound scan");

    /* Bound handle with invalid interface returns -EINVAL */
    int dummy_ctx = 0;
    i_network_api_t dummy_api = {0};
    i_network_t bound = {
        .api = &dummy_api,
        .impl = &dummy_ctx,
    };
    zassert_true(i_network_is_bound(&bound), "Bound check true");

    zassert_equal(i_network_init(&bound, NETWORK_INTERFACE_MAX), -EINVAL, "Invalid iface init");
    zassert_equal(i_network_start(&bound, NETWORK_INTERFACE_MAX), -EINVAL, "Invalid iface start");
    zassert_equal(i_network_stop(&bound, NETWORK_INTERFACE_MAX), -EINVAL, "Invalid iface stop");
    zassert_equal(i_network_get_config(&bound, NETWORK_INTERFACE_MAX, &cfg, sizeof(cfg), &out_size), -EINVAL, "Invalid iface get_cfg");
    zassert_equal(i_network_set_config(&bound, NETWORK_INTERFACE_MAX, &cfg, sizeof(cfg)), -EINVAL, "Invalid iface set_cfg");
    zassert_equal(i_network_get_status(&bound, NETWORK_INTERFACE_MAX, &status), -EINVAL, "Invalid iface get_status");
    zassert_equal(i_network_scan(&bound, NETWORK_INTERFACE_MAX), -EINVAL, "Invalid iface scan");

    /* Bound handle with null output pointers returns -EINVAL */
    zassert_equal(i_network_get_config(&bound, NETWORK_INTERFACE_ETHERNET, NULL, sizeof(cfg), &out_size), -EINVAL, "NULL out_data");
    zassert_equal(i_network_get_config(&bound, NETWORK_INTERFACE_ETHERNET, &cfg, 0, &out_size), -EINVAL, "Zero max_size");
    zassert_equal(i_network_set_config(&bound, NETWORK_INTERFACE_ETHERNET, NULL, sizeof(cfg)), -EINVAL, "NULL set data");
    zassert_equal(i_network_set_config(&bound, NETWORK_INTERFACE_ETHERNET, &cfg, 0), -EINVAL, "Zero set size");
    zassert_equal(i_network_get_status(&bound, NETWORK_INTERFACE_ETHERNET, NULL), -EINVAL, "NULL out_status");
}

ZTEST(network_suite, test_fake_network_lifecycle_and_operations)
{
    fake_network_reset();

    i_network_t eth_port = fake_network_get_eth_port();
    i_network_t wifi_port = fake_network_get_wifi_port();

    zassert_true(i_network_is_bound(&eth_port), "ETH port bound");
    zassert_true(i_network_is_bound(&wifi_port), "WiFi port bound");

    /* Initial state is DOWN */
    net_status_t status;
    zassert_ok(i_network_get_status(&eth_port, NETWORK_INTERFACE_ETHERNET, &status), "Get ETH status");
    zassert_equal(status.state, NET_IF_STATE_DOWN, "Initial ETH state DOWN");

    zassert_ok(i_network_get_status(&wifi_port, NETWORK_INTERFACE_WIFI, &status), "Get WiFi status");
    zassert_equal(status.state, NET_IF_STATE_DOWN, "Initial WiFi state DOWN");

    /* Init & Start */
    zassert_ok(i_network_init(&eth_port, NETWORK_INTERFACE_ETHERNET), "Init ETH");
    zassert_ok(i_network_start(&eth_port, NETWORK_INTERFACE_ETHERNET), "Start ETH");

    zassert_ok(i_network_get_status(&eth_port, NETWORK_INTERFACE_ETHERNET, &status), "Get ETH status");
    zassert_equal(status.state, NET_IF_STATE_CONNECTING, "Started ETH state CONNECTING");

    /* Simulate link up / IP connected */
    fake_network_set_state(NETWORK_INTERFACE_ETHERNET, NET_IF_STATE_CONNECTED, 0xC0A80164, 0xFFFFFF00, 0xC0A80101);
    zassert_ok(i_network_get_status(&eth_port, NETWORK_INTERFACE_ETHERNET, &status), "Get ETH status after connect");
    zassert_equal(status.state, NET_IF_STATE_CONNECTED, "ETH connected");
    zassert_equal(status.ip_addr, 0xC0A80164, "ETH IP verified");

    /* Config Ethernet */
    net_eth_config_t eth_cfg = {
        .ip_mode = NET_IP_MODE_STATIC,
        .mac_addr = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
        .ip_addr = 0x0A000005,
        .subnet_mask = 0xFF000000,
        .gateway = 0x0A000001,
        .auto_dhcpv4 = false,
    };
    zassert_ok(i_network_set_config(&eth_port, NETWORK_INTERFACE_ETHERNET, &eth_cfg, sizeof(eth_cfg)), "Set ETH config");

    net_eth_config_t eth_cfg_read;
    size_t out_len = 0;
    zassert_ok(i_network_get_config(&eth_port, NETWORK_INTERFACE_ETHERNET, &eth_cfg_read, sizeof(eth_cfg_read), &out_len), "Get ETH config");
    zassert_equal(out_len, sizeof(net_eth_config_t), "Config length matches");
    zassert_equal(eth_cfg_read.ip_addr, 0x0A000005, "IP matches configured static IP");

    /* Scan on Ethernet must return -ENOSYS */
    zassert_equal(i_network_scan(&eth_port, NETWORK_INTERFACE_ETHERNET), -ENOSYS, "Scan on ETH must return -ENOSYS");

    /* Scan on WiFi succeeds */
    zassert_ok(i_network_init(&wifi_port, NETWORK_INTERFACE_WIFI), "Init WiFi");
    zassert_ok(i_network_start(&wifi_port, NETWORK_INTERFACE_WIFI), "Start WiFi");
    zassert_ok(i_network_scan(&wifi_port, NETWORK_INTERFACE_WIFI), "Scan on WiFi must return 0");

    /* Config WiFi */
    net_wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    wifi_cfg.mode = NET_WIFI_MODE_STA;
    strncpy(wifi_cfg.sta.ssid, "TestRouter", sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy(wifi_cfg.sta.psk, "PasswordSecret", sizeof(wifi_cfg.sta.psk) - 1);
    wifi_cfg.sta.security = NET_WIFI_SEC_WPA2_PSK;
    wifi_cfg.sta.ip_mode = NET_IP_MODE_DHCP;
    zassert_ok(i_network_set_config(&wifi_port, NETWORK_INTERFACE_WIFI, &wifi_cfg, sizeof(wifi_cfg)), "Set WiFi config");

    net_wifi_config_t wifi_cfg_read;
    zassert_ok(i_network_get_config(&wifi_port, NETWORK_INTERFACE_WIFI, &wifi_cfg_read, sizeof(wifi_cfg_read), &out_len), "Get WiFi config");
    zassert_equal(out_len, sizeof(net_wifi_config_t), "WiFi config length matches");
    zassert_str_equal(wifi_cfg_read.sta.ssid, "TestRouter", "SSID matches");

    /* Stop */
    zassert_ok(i_network_stop(&eth_port, NETWORK_INTERFACE_ETHERNET), "Stop ETH");
    zassert_ok(i_network_get_status(&eth_port, NETWORK_INTERFACE_ETHERNET, &status), "Status after stop");
    zassert_equal(status.state, NET_IF_STATE_DOWN, "State DOWN after stop");

    /* Inspect fake counters */
    const fake_network_state_t *state = fake_network_get_state();
    zassert_true(state->eth_init_calls >= 1, "ETH init call count");
    zassert_true(state->eth_start_calls >= 1, "ETH start call count");
    zassert_true(state->eth_stop_calls >= 1, "ETH stop call count");
    zassert_true(state->wifi_scan_calls >= 1, "WiFi scan call count");

    /* Injected error check */
    fake_network_set_return_code(NETWORK_INTERFACE_ETHERNET, -EIO);
    zassert_equal(i_network_start(&eth_port, NETWORK_INTERFACE_ETHERNET), -EIO, "Injected error returned");
}

ZTEST(network_suite, test_network_defaults_factory)
{
    net_eth_config_t eth_cfg;
    net_wifi_config_t wifi_cfg;

    /* Invalid arguments */
    zassert_equal(network_default_eth_config(NULL), -EINVAL, "Null eth_cfg");
    zassert_equal(network_default_wifi_config(NULL), -EINVAL, "Null wifi_cfg");

    /* Ethernet Defaults */
    zassert_ok(network_default_eth_config(&eth_cfg), "Populate default ETH config");
    zassert_equal(eth_cfg.ip_mode, NET_IP_MODE_DHCP, "Default ETH mode must be DHCP");
    zassert_equal(eth_cfg.auto_dhcpv4, 1U, "Default ETH auto_dhcpv4 must be 1");
    zassert_equal(eth_cfg.ip_addr, 0xC0A80164, "Static fallback IP 192.168.1.100");
    zassert_equal(eth_cfg.subnet_mask, 0xFFFFFF00, "Static fallback mask 255.255.255.0");
    zassert_equal(eth_cfg.gateway, 0xC0A80101, "Static fallback GW 192.168.1.1");
    zassert_true(net_eth_config_is_valid(&eth_cfg), "Default ETH config must pass validation");

    /* WiFi Defaults */
    zassert_ok(network_default_wifi_config(&wifi_cfg), "Populate default WiFi config");
    zassert_equal(wifi_cfg.mode, NET_WIFI_MODE_AP, "Default WiFi mode must be Soft-AP");
    zassert_str_equal(wifi_cfg.ap.ssid, "BrightC-AP", "Default AP SSID");
    zassert_str_equal(wifi_cfg.ap.psk, "12345678", "Default AP PSK");
    zassert_equal(wifi_cfg.ap.security, NET_WIFI_SEC_WPA2_PSK, "Default AP Security WPA2-PSK");
    zassert_equal(wifi_cfg.ap.channel, 9U, "Default AP channel 9");
    zassert_equal(wifi_cfg.ap.max_clients, 5U, "Default AP max clients 5");
    zassert_equal(wifi_cfg.ap.ap_ip_addr, 0xC0A80401, "Default AP IP 192.168.4.1");
    zassert_equal(wifi_cfg.ap.ap_subnet_mask, 0xFFFFFF00, "Default AP subnet mask 255.255.255.0");
    zassert_true(net_wifi_config_is_valid(&wifi_cfg), "Default WiFi config must pass validation");
}

/* Stacks allocated at file scope for ao_network test */
static K_THREAD_STACK_DEFINE(s_test_ao_net_stack, AO_NETWORK_STACK_SIZE);
static ao_network_t s_test_ao_net;
static struct k_msgq s_test_ao_net_queue;
static uint8_t s_test_ao_net_queue_buf[AO_NETWORK_QUEUE_DEPTH * sizeof(ao_network_event_t)];

ZTEST(network_suite, test_ao_network_init_validation)
{
    fake_network_reset();
    i_network_t eth_port = fake_network_get_eth_port();
    i_network_t wifi_port = fake_network_get_wifi_port();

    k_msgq_init(&s_test_ao_net_queue, s_test_ao_net_queue_buf, sizeof(ao_network_event_t), AO_NETWORK_QUEUE_DEPTH);

    /* NULL checks for required arguments */
    zassert_equal(ao_network_init(NULL, &eth_port, &wifi_port, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), -EINVAL, "Null self");
    zassert_equal(ao_network_init(&s_test_ao_net, NULL, &wifi_port, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), -EINVAL, "Null eth");
    zassert_equal(ao_network_init(&s_test_ao_net, &eth_port, NULL, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), -EINVAL, "Null wifi");
    zassert_equal(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, NULL, NULL, NULL, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), -EINVAL, "Null queue");
    zassert_equal(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, NULL, NULL, &s_test_ao_net_queue, NULL, AO_NETWORK_STACK_SIZE), -EINVAL, "Null stack");
    zassert_equal(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, 0), -EINVAL, "Zero stack size");

    /* Valid init with NULL initial configs (domain default fallback) */
    zassert_ok(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), "Valid ao_network init with NULL defaults");
    zassert_equal(s_test_ao_net.eth_cfg.ip_mode, NET_IP_MODE_DHCP, "ETH fallback is DHCP");
    zassert_equal(s_test_ao_net.eth_cfg.ip_addr, 0xC0A80164, "ETH fallback IP 192.168.1.100");
    zassert_equal(s_test_ao_net.wifi_cfg.mode, NET_WIFI_MODE_AP, "WiFi fallback is AP");
    zassert_str_equal(s_test_ao_net.wifi_cfg.ap.ssid, "BrightC-AP", "WiFi fallback SSID");

    /* Valid init with injected custom initial configs */
    net_eth_config_t custom_eth = {
        .ip_mode = NET_IP_MODE_STATIC,
        .ip_addr = 0x0A00000A,
        .subnet_mask = 0xFFFFFF00,
        .gateway = 0x0A000001,
        .auto_dhcpv4 = false,
    };
    net_wifi_config_t custom_wifi;
    memset(&custom_wifi, 0, sizeof(custom_wifi));
    custom_wifi.mode = NET_WIFI_MODE_STA;
    strncpy(custom_wifi.sta.ssid, "CustomAP", sizeof(custom_wifi.sta.ssid) - 1);
    strncpy(custom_wifi.sta.psk, "CustomPassword", sizeof(custom_wifi.sta.psk) - 1);
    custom_wifi.sta.security = NET_WIFI_SEC_WPA2_PSK;
    custom_wifi.sta.ip_mode = NET_IP_MODE_STATIC;
    custom_wifi.sta.ip_addr = 0x0A000014;
    custom_wifi.sta.subnet_mask = 0xFFFFFF00;
    custom_wifi.sta.gateway = 0x0A000001;

    zassert_ok(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, &custom_eth, &custom_wifi, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), "Valid ao_network init with custom configs");
    zassert_equal(s_test_ao_net.eth_cfg.ip_mode, NET_IP_MODE_STATIC, "Injected ETH mode static");
    zassert_equal(s_test_ao_net.eth_cfg.ip_addr, 0x0A00000A, "Injected ETH IP");
    zassert_equal(s_test_ao_net.wifi_cfg.mode, NET_WIFI_MODE_STA, "Injected WiFi mode STA");
    zassert_str_equal(s_test_ao_net.wifi_cfg.sta.ssid, "CustomAP", "Injected WiFi SSID");
}

ZTEST(network_suite, test_ao_network_lifecycle_and_zbus_publication)
{
    fake_network_reset();
    i_network_t eth_port = fake_network_get_eth_port();
    i_network_t wifi_port = fake_network_get_wifi_port();

    k_msgq_init(&s_test_ao_net_queue, s_test_ao_net_queue_buf, sizeof(ao_network_event_t), AO_NETWORK_QUEUE_DEPTH);
    zassert_ok(ao_network_init(&s_test_ao_net, &eth_port, &wifi_port, NULL, NULL, &s_test_ao_net_queue, s_test_ao_net_stack, AO_NETWORK_STACK_SIZE), "Init ao_network");

    /* Start Active Object */
    zassert_ok(ao_network_start(&s_test_ao_net), "Start ao_network");
    zassert_equal(ao_network_start(&s_test_ao_net), -EALREADY, "Double start must return -EALREADY");

    /* Verify that both adapters were initialized, configured with defaults, and started */
    const fake_network_state_t *state = fake_network_get_state();
    zassert_true(state->eth_init_calls >= 1, "ETH init invoked");
    zassert_true(state->eth_set_config_calls >= 1, "ETH set_config with defaults invoked");
    zassert_true(state->eth_start_calls >= 1, "ETH start invoked");

    zassert_true(state->wifi_init_calls >= 1, "WiFi init invoked");
    zassert_true(state->wifi_set_config_calls >= 1, "WiFi set_config with defaults invoked");
    zassert_true(state->wifi_start_calls >= 1, "WiFi start invoked");

    /* Default configs applied in fake */
    zassert_equal(state->eth_config.ip_mode, NET_IP_MODE_DHCP, "ETH configured with DHCP default");
    zassert_equal(state->wifi_config.mode, NET_WIFI_MODE_AP, "WiFi configured with AP default");

    /* Run synchronous step and verify initial Zbus channel readings */
    zassert_ok(ao_network_sync_step(&s_test_ao_net), "Sync step");

    net_state_msg_t eth_msg;
    net_state_msg_t wifi_msg;
    zassert_ok(zbus_chan_read(&chan_network_eth_state, &eth_msg, K_MSEC(100)), "Read chan_network_eth_state");
    zassert_ok(zbus_chan_read(&chan_network_wifi_state, &wifi_msg, K_MSEC(100)), "Read chan_network_wifi_state");

    zassert_equal(eth_msg.interface, NETWORK_INTERFACE_ETHERNET, "ETH channel interface");
    zassert_equal(eth_msg.status.state, NET_IF_STATE_CONNECTING, "ETH status CONNECTING after start");
    zassert_equal(wifi_msg.interface, NETWORK_INTERFACE_WIFI, "WiFi channel interface");
    zassert_equal(wifi_msg.status.state, NET_IF_STATE_CONNECTING, "WiFi status CONNECTING after start");

    /* Simulate ETH Link UP & IP Assigned */
    fake_network_set_state(NETWORK_INTERFACE_ETHERNET, NET_IF_STATE_CONNECTED, 0xC0A8017B, 0xFFFFFF00, 0xC0A80101);
    zassert_ok(ao_network_sync_step(&s_test_ao_net), "Sync step after state change");

    zassert_ok(zbus_chan_read(&chan_network_eth_state, &eth_msg, K_MSEC(100)), "Read updated chan_network_eth_state");
    zassert_equal(eth_msg.status.state, NET_IF_STATE_CONNECTED, "ETH connected");
    zassert_equal(eth_msg.status.ip_addr, 0xC0A8017B, "ETH IP updated in Zbus");

    /* Post hot reconfiguration for WiFi to STA */
    ao_network_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = AO_NETWORK_EVENT_CONFIG_WIFI;
    evt.data.wifi_cfg.mode = NET_WIFI_MODE_STA;
    strncpy(evt.data.wifi_cfg.sta.ssid, "MyHomeNet", sizeof(evt.data.wifi_cfg.sta.ssid) - 1);
    strncpy(evt.data.wifi_cfg.sta.psk, "SuperSecret123", sizeof(evt.data.wifi_cfg.sta.psk) - 1);
    evt.data.wifi_cfg.sta.security = NET_WIFI_SEC_WPA2_PSK;
    evt.data.wifi_cfg.sta.ip_mode = NET_IP_MODE_DHCP;

    zassert_ok(ao_network_post(&s_test_ao_net, &evt), "Post WiFi config event");

    /* Allow Active Object thread to process event */
    k_msleep(600U);

    /* Verify WiFi configuration was updated */
    state = fake_network_get_state();
    zassert_equal(state->wifi_config.mode, NET_WIFI_MODE_STA, "WiFi mode changed to STA");
    zassert_str_equal(state->wifi_config.sta.ssid, "MyHomeNet", "SSID updated");

    /* Stop ao_network */
    zassert_ok(ao_network_stop(&s_test_ao_net), "Stop ao_network");
}


