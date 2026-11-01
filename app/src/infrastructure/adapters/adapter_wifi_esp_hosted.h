/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi concrete adapter for ESP-Hosted-MCU (SPI4) behind i_network.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/net_if.h>

#include "common/network_types.h"
#include "interfaces/i_network.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context structure for the ESP-Hosted-MCU Wi-Fi adapter.
 *
 * Stored by value in app_container_t. Zero dynamic allocation.
 */
typedef struct {
    const struct device *wifi_dev; /**< Device pointer for esp_hosted_mcu_wifi */
    struct net_if *sta_iface;      /**< Bound station interface */
    struct net_if *ap_iface;       /**< Bound soft-AP interface (if supported) */
    net_wifi_config_t cfg;         /**< Current Wi-Fi configuration */
    net_status_t status;           /**< Last reported status */
    uint32_t link_up_time;         /**< Timestamp (ms) when link became UP */
    bool is_ready;                 /**< True once initialized */
    bool is_started;               /**< True once started */
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

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_WIFI_ESP_HOSTED_H_ */
