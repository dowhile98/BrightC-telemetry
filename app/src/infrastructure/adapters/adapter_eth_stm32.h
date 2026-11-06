/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet concrete adapter for STM32 RMII LAN8742 PHY behind i_network.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ETH_STM32_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ETH_STM32_H_

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
 * @brief Context structure for the STM32 Ethernet adapter.
 *
 * Stored by value in app_container_t. Zero dynamic allocation.
 */
typedef struct {
    const struct device *eth_dev;      /**< Device pointer (e.g. eth0 / mac) */
    struct net_if *iface;              /**< Bound Zephyr network interface */
    net_eth_config_t cfg;              /**< Current Ethernet configuration */
    net_status_t status;               /**< Last reported status */
    uint32_t link_up_time;             /**< Timestamp (ms) when carrier link came UP */
    net_adapter_fsm_state_t fsm_state; /**< Explicit finite state machine state */
} adapter_eth_stm32_t;

/**
 * @brief Initialize the STM32 Ethernet adapter context and bind the network interface.
 *
 * @param[out] ctx     Adapter context instance.
 * @param[in]  eth_dev Zephyr device pointer for the Ethernet controller.
 * @return 0 on success, negative errno on failure.
 */
int adapter_eth_stm32_init(adapter_eth_stm32_t *ctx, const struct device *eth_dev);

/**
 * @brief Produce an i_network_t port handle bound to this adapter instance.
 *
 * @param[in] ctx Adapter context instance.
 * @return Bound i_network_t port handle.
 */
i_network_t adapter_eth_stm32_port(adapter_eth_stm32_t *ctx);

/**
 * @brief Get the current FSM state of the STM32 Ethernet adapter.
 *
 * @param[in] ctx Adapter context instance.
 * @return Current net_adapter_fsm_state_t value.
 */
net_adapter_fsm_state_t adapter_eth_stm32_get_fsm_state(const adapter_eth_stm32_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ETH_STM32_H_ */
