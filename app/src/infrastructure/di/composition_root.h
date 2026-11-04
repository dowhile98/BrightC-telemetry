/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Composition root: the single place where every object of the application is
 * created, wired and started. Nothing else in the firmware holds mutable
 * static data; the only static instance is the container declared in main.c.
 */

#ifndef APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_
#define APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include "application/active_objects/ao_digital_input.h"
#include "application/active_objects/ao_modbus.h"
#include "application/active_objects/ao_network.h"
#include "application/active_objects/ao_storage.h"
#include "application/active_objects/ao_system_indicator.h"
#include "application/modbus/modbus_registry.h"
#include "common/config_types.h"
#include "common/digital_input_types.h"
#include "common/indicator_types.h"
#include "common/modbus_types.h"
#include "common/network_types.h"
#include "domain/services/digital_input_aggregator.h"
#include "interfaces/i_buzzer.h"
#include "interfaces/i_config_storage.h"
#include "interfaces/i_digital_input.h"
#include "interfaces/i_led.h"
#include "interfaces/i_modbus_client.h"
#include "interfaces/i_network.h"
#include "interfaces/i_relay.h"
#include "interfaces/i_slave_registry.h"

#ifdef CONFIG_NANOMODBUS
#include "infrastructure/adapters/adapter_modbus_nmbs.h"
#endif
#include "infrastructure/adapters/adapter_relay_modbus.h"
#ifndef CONFIG_ZTEST
#include "infrastructure/adapters/adapter_eth_stm32.h"
#include "infrastructure/adapters/adapter_wifi_esp_hosted.h"
#include "infrastructure/adapters/adapter_storage_microconf.h"
#include "infrastructure/adapters/adapter_led_gpio.h"
#include "infrastructure/adapters/adapter_buzzer_pwm.h"
#include "infrastructure/adapters/adapter_di_gpio.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Init mask bits. */
#define APP_CONTAINER_INIT_REGISTRY 0x01U
#define APP_CONTAINER_INIT_AO 0x02U
#define APP_CONTAINER_INIT_TRANSPORT 0x04U
#define APP_CONTAINER_INIT_NETWORK 0x08U
#define APP_CONTAINER_INIT_STORAGE 0x10U
#define APP_CONTAINER_INIT_INDICATORS 0x20U
#define APP_CONTAINER_INIT_DIGITAL_INPUT 0x40U

/**
 * @brief Every object of the application, wired together.
 *
 * Declared once (`static app_container_t g_app` in main.c) and passed by
 * reference to every component. Adapters live here by value so that no module
 * needs file-scope mutable state.
 *
 * The container must be zero-initialised before the first call to
 * composition_root_init(): either declare it `static` or `memset` it first.
 * Thread stacks are defined separately at file scope with K_THREAD_STACK_ARRAY_DEFINE
 * to satisfy MPU alignment and guard constraints.
 */
typedef struct {
    /* Ports exposed to the rest of the application */
    i_slave_registry_t registry;              /**< Slave registry port */
    i_modbus_client_t client[MODBUS_BUS_MAX]; /**< One client port per bus */
    i_relay_t relay_ext;                      /**< External relay port (Waveshare Modbus 32CH) */
    i_relay_t relay_int;                      /**< Internal relay port (placeholder for onboard GPIO / MCP23S17) */
    i_network_t net_eth;                      /**< Ethernet network interface port */
    i_network_t net_wifi;                     /**< WiFi network interface port */
    i_config_storage_t storage;               /**< Persistent storage port */
    i_led_t led;                              /**< Status LED port */
    i_buzzer_t buzzer;                        /**< Buzzer acoustic port */
    i_digital_input_t digital_inputs;         /**< Digital input and contact alarm port */

    /* Adapters, stored by value */
    modbus_registry_t registry_impl;          /**< Concrete registry */
    di_aggregator_t di_aggregator;            /**< Concrete digital input aggregator */
#ifdef CONFIG_NANOMODBUS
    adapter_modbus_nmbs_t nmbs[MODBUS_BUS_MAX]; /**< nanoMODBUS client per bus */
#endif
    adapter_relay_modbus_t waveshare_relay;   /**< Waveshare relay module adapter */
#ifndef CONFIG_ZTEST
    adapter_eth_stm32_t eth_adapter;          /**< STM32 Ethernet adapter */
    adapter_wifi_esp_hosted_t wifi_adapter;   /**< ESP-Hosted-MCU WiFi adapter */
    adapter_storage_microconf_t storage_adapter; /**< microconf EEPROM storage adapter */
    adapter_led_gpio_t led_adapter;           /**< GPIO Status LED adapter */
    adapter_buzzer_pwm_t buzzer_adapter;      /**< PWM Buzzer adapter */
    adapter_di_gpio_t di_adapter;             /**< GPIO Digital Input adapter */
#endif

    /* Active objects and their static resources */
    ao_modbus_t ao[MODBUS_BUS_MAX];             /**< One polling thread per bus */
    struct k_msgq ao_queue[MODBUS_BUS_MAX];     /**< Command queues */
    uint8_t ao_queue_buf[MODBUS_BUS_MAX][AO_MODBUS_QUEUE_DEPTH * sizeof(modbus_event_t)];

    ao_network_t ao_net;                        /**< Network active object */
    struct k_msgq ao_net_queue;                 /**< Network AO command queue */
    uint8_t ao_net_queue_buf[AO_NETWORK_QUEUE_DEPTH * sizeof(ao_network_event_t)];

    ao_storage_t ao_store;                      /**< Storage active object coordinator */
    struct k_msgq ao_store_queue;               /**< Storage command queue */
    uint8_t ao_store_queue_buf[AO_STORAGE_QUEUE_DEPTH * sizeof(ao_storage_event_t)];

    ao_system_indicator_t ao_indicator;         /**< System indicator active object */
    struct k_msgq ao_indicator_queue;           /**< Indicator AO command queue */
    uint8_t ao_indicator_queue_buf[AO_INDICATOR_QUEUE_DEPTH * sizeof(ao_indicator_event_t)];

    ao_digital_input_t ao_di;                   /**< Digital input active object */
    struct k_msgq ao_di_queue;                  /**< Digital input AO command queue */
    uint8_t ao_di_queue_buf[AO_DI_QUEUE_DEPTH * sizeof(ao_di_event_t)];

    /* Configuration of the physical buses */
    modbus_bus_cfg_t bus_cfg[MODBUS_BUS_MAX];

    uint8_t init_mask; /**< APP_CONTAINER_INIT_* bits already applied */
} app_container_t;

/**
 * @brief Create and wire every object. Idempotent.
 *
 * @param[out] self Container to initialise.
 *
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init(app_container_t *self);

/**
 * @brief Start the Active Objects. Idempotent.
 *
 * @param[in,out] self Container previously initialised.
 *
 * @return 0 on success, -EINVAL when @p self is not initialised.
 */
int composition_root_start(app_container_t *self);

/**
 * @brief Stop the Active Objects. Idempotent.
 *
 * @param[in,out] self Container previously initialised.
 *
 * @return 0 on success, -EINVAL when @p self is not initialised.
 */
int composition_root_stop(app_container_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_ */
