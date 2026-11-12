/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi concrete adapter for ESP-Hosted-MCU (SPI4) behind i_network and i_wifi_control.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

#include "common/network_types.h"
#include "interfaces/i_network.h"
#include "interfaces/i_wifi_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context structure for the ESP-Hosted-MCU Wi-Fi adapter.
 *
 * Stored by value in app_container_t. Zero dynamic allocation.
 */
typedef struct {
    const struct device *wifi_dev;      /**< Device pointer for esp_hosted_mcu_wifi */
    struct net_if *sta_iface;           /**< Bound station interface */
    struct net_if *ap_iface;            /**< Bound soft-AP interface (if supported) */
    net_wifi_config_t cfg;              /**< Current Wi-Fi configuration */
    net_status_t status;                /**< Last reported status */
    uint32_t link_up_time;              /**< Timestamp (ms) when link became UP */
    net_adapter_fsm_state_t fsm_state;  /**< Explicit finite state machine state */
#if defined(CONFIG_NET_MGMT_EVENT)
    struct net_mgmt_event_callback wifi_mgmt_cb; /**< Wi-Fi subsystem event subscription */
    struct net_mgmt_event_callback ipv4_mgmt_cb; /**< IPv4 address event subscription */
#endif
#define ADAPTER_WIFI_MAX_SCAN_RESULTS 16U
    net_wifi_scan_entry_t scan_results[ADAPTER_WIFI_MAX_SCAN_RESULTS]; /**< Cached AP scan results */
    size_t scan_count;                   /**< Number of valid scan entries */
} adapter_wifi_esp_hosted_t;

/**
 * @brief Initialize the Wi-Fi adapter context and bind network interfaces.
 *
 * @param[out] ctx      Adapter context instance.
 * @param[in]  wifi_dev Zephyr device pointer for the Wi-Fi controller.
 * @return 0 on success, negative errno on failure.
 */
int adapter_wifi_esp_hosted_init(adapter_wifi_esp_hosted_t *ctx, const struct device *wifi_dev);

/**
 * @brief Produce an i_network_t port handle bound to this adapter instance.
 *
 * @param[in] ctx Adapter context instance.
 * @return Bound i_network_t port handle.
 */
i_network_t adapter_wifi_esp_hosted_port(adapter_wifi_esp_hosted_t *ctx);

/**
 * @brief Produce an i_wifi_control_t port handle bound to this adapter instance.
 *
 * @param[in] ctx Adapter context instance.
 * @return Bound i_wifi_control_t port handle.
 */
i_wifi_control_t adapter_wifi_esp_hosted_wifi_control_port(adapter_wifi_esp_hosted_t *ctx);

/**
 * @brief Get the current FSM state of the ESP-Hosted Wi-Fi adapter.
 *
 * @param[in] ctx Adapter context instance.
 * @return Current net_adapter_fsm_state_t value.
 */
net_adapter_fsm_state_t adapter_wifi_esp_hosted_get_fsm_state(const adapter_wifi_esp_hosted_t *ctx);

/**
 * @brief Retrieve cached Wi-Fi AP scan results.
 *
 * @param[in]  ctx         Adapter context instance.
 * @param[out] out_entries Destination buffer for scan results.
 * @param[in]  max_entries Maximum entries to copy.
 * @param[out] out_count   Pointer to receive total available entries.
 * @return 0 on success, negative errno on failure.
 */
int adapter_wifi_esp_hosted_get_scan_results(adapter_wifi_esp_hosted_t *ctx,
                                             net_wifi_scan_entry_t *out_entries,
                                             size_t max_entries,
                                             size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_ */
