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

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "application/active_objects/ao_digital_input.h"
#include "application/active_objects/ao_hvac.h"
#include "application/active_objects/ao_modbus.h"
#include "application/active_objects/ao_network.h"
#include "application/active_objects/ao_storage.h"
#include "application/active_objects/ao_system_indicator.h"
#include "application/modbus/modbus_registry.h"
#include "common/config_types.h"
#include "common/default_config_profiles.h"
#include "common/digital_input_types.h"
#include "common/hvac_types.h"
#include "common/indicator_types.h"
#include "common/modbus_types.h"
#include "common/network_types.h"
#include "domain/services/digital_input_aggregator.h"
#include "interfaces/i_buzzer.h"
#include "interfaces/i_config_storage.h"
#include "interfaces/i_digital_input.h"
#include "interfaces/i_hvac_unit.h"
#include "interfaces/i_led.h"
#include "interfaces/i_modbus_client.h"
#include "interfaces/i_network.h"
#include "interfaces/i_wifi_control.h"
#include "interfaces/i_relay.h"
#include "interfaces/i_slave_registry.h"

#ifdef CONFIG_NANOMODBUS
#include "infrastructure/adapters/adapter_modbus_nmbs.h"
#endif
#include "infrastructure/adapters/adapter_relay_modbus.h"
#include "infrastructure/adapters/adapter_hvac_actuator.h"
#include "infrastructure/adapters/adapter_relay_gpio.h"
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

/**
 * @brief Every object of the application, wired together.
 *
 * Organized vertically by subsystem:
 * - Subsystem 1: Modbus Master & Relays (`modbus`)
 * - Subsystem 2: Digital Inputs & Contact Alarms (`di`)
 * - Subsystem 3: Configuration & Persistent Storage (`storage`)
 * - Subsystem 4: Network (`network`)
 * - Subsystem 5: System Indicators (`indicators`)
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
    /* Subsystem 1: Modbus Master & Relays */
    struct {
        i_slave_registry_t registry_port;              /**< Slave registry port */
        i_modbus_client_t client_port[MODBUS_BUS_MAX]; /**< One client port per bus */
        i_relay_t relay_ext_port;                      /**< External relay port (Waveshare Modbus 32CH) */
        i_relay_t relay_int_port;                      /**< Internal relay port (placeholder) */
        modbus_registry_t registry_impl;               /**< Concrete registry */
        adapter_relay_modbus_t waveshare_relay;        /**< Waveshare relay module adapter */
#ifdef CONFIG_NANOMODBUS
        adapter_modbus_nmbs_t nmbs[MODBUS_BUS_MAX];    /**< nanoMODBUS client per bus */
#endif
        ao_modbus_t ao[MODBUS_BUS_MAX];                /**< One polling thread per bus */
        struct k_msgq ao_queue[MODBUS_BUS_MAX];        /**< Command queues */
        uint8_t ao_queue_buf[MODBUS_BUS_MAX][AO_MODBUS_QUEUE_DEPTH * sizeof(modbus_event_t)];
        modbus_bus_cfg_t bus_cfg[MODBUS_BUS_MAX];      /**< Physical bus configurations */
        bool initialized;                              /**< Subsystem initialization status */
    } modbus;

    /* Subsystem 2: Digital Inputs & Contact Alarms */
    struct {
        i_digital_input_t port;                        /**< Digital input and contact alarm port */
        di_aggregator_t aggregator;                    /**< Concrete digital input aggregator */
#ifndef CONFIG_ZTEST
        adapter_di_gpio_t di_adapter;                  /**< GPIO Digital Input adapter */
#endif
        ao_digital_input_t ao;                         /**< Digital input active object */
        struct k_msgq queue;                           /**< Digital input AO command queue */
        uint8_t queue_buf[AO_DI_QUEUE_DEPTH * sizeof(ao_di_event_t)];
        bool initialized;                              /**< Subsystem initialization status */
    } di;

    /* Subsystem 3: Configuration & Persistent Storage */
    struct {
        i_config_storage_t port;                       /**< Persistent storage port */
#ifndef CONFIG_ZTEST
        adapter_storage_microconf_t storage_adapter;   /**< microconf EEPROM storage adapter */
#endif
        ao_storage_t ao;                               /**< Storage active object coordinator */
        struct k_msgq queue;                           /**< Storage command queue */
        uint8_t queue_buf[AO_STORAGE_QUEUE_DEPTH * sizeof(ao_storage_event_t)];
        bool initialized;                              /**< Subsystem initialization status */
    } storage;

    /* Subsystem 4: Network (Ethernet LAN8742 & WiFi ESP-Hosted) */
    struct {
        i_network_t eth_port;                          /**< Ethernet network interface port */
        i_network_t wifi_port;                         /**< WiFi network interface port */
        i_wifi_control_t wifi_ctrl_port;               /**< WiFi control port */
#ifndef CONFIG_ZTEST
        adapter_eth_stm32_t eth_adapter;               /**< STM32 Ethernet adapter */
        adapter_wifi_esp_hosted_t wifi_adapter;        /**< ESP-Hosted-MCU WiFi adapter */
#endif
        ao_network_t ao;                               /**< Network active object */
        struct k_msgq queue;                           /**< Network AO command queue */
        uint8_t queue_buf[AO_NETWORK_QUEUE_DEPTH * sizeof(ao_network_event_t)];
        bool initialized;                              /**< Subsystem initialization status */
    } network;

    /* Subsystem 5: System Indicators (LED & Buzzer) */
    struct {
        i_led_t led_port;                              /**< Status LED port */
        i_buzzer_t buzzer_port;                        /**< Buzzer acoustic port */
#ifndef CONFIG_ZTEST
        adapter_led_gpio_t led_adapter;                /**< GPIO Status LED adapter */
        adapter_buzzer_pwm_t buzzer_adapter;           /**< PWM Buzzer adapter */
#endif
        ao_system_indicator_t ao;                      /**< System indicator active object */
        struct k_msgq queue;                           /**< Indicator AO command queue */
        uint8_t queue_buf[AO_INDICATOR_QUEUE_DEPTH * sizeof(ao_indicator_event_t)];
        bool initialized;                              /**< Subsystem initialization status */
    } indicators;

    /* Subsystem 6: Industrial HVAC Control */
    struct {
        i_hvac_unit_t actuator_port;                   /**< Actuator port handle */
        adapter_hvac_actuator_t actuator_adapter;      /**< Actuator adapter instance */
        i_relay_t local_relay_port;                    /**< Local internal relay port handle */
        adapter_relay_gpio_t local_relay_adapter;      /**< Local internal GPIO relay adapter instance */
        ao_hvac_t ao;                                  /**< HVAC Active Object coordinator */
        struct k_msgq queue;                           /**< HVAC AO command queue */
        uint8_t queue_buf[AO_HVAC_QUEUE_DEPTH * sizeof(ao_hvac_event_t)];
        config_hvac_t config;                          /**< Persistent HVAC configuration */
        bool initialized;                              /**< Subsystem initialization status */
    } hvac;

    /* Container-level status */
    bool is_initialized;                               /**< True when container is initialized */
    bool is_running;                                   /**< True when Active Objects are running */
} app_container_t;

/* =========================================================================
 * Modular Subsystem Initializers
 * ========================================================================= */

/**
 * @brief Initialize the Modbus subsystem and relays.
 *
 * Configures transports, registry, Active Objects, and relay adapters.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_modbus(app_container_t *self);

/**
 * @brief Initialize the digital inputs subsystem.
 *
 * Configures aggregator, hardware adapter (or fake), and digital input AO.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_digital_inputs(app_container_t *self);

/**
 * @brief Initialize the persistent storage subsystem.
 *
 * Configures EEPROM storage adapter (or fake) and storage coordinator AO.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_storage(app_container_t *self);

/**
 * @brief Initialize the network subsystem.
 *
 * Configures Ethernet and WiFi adapters, loads persistent network configurations,
 * and initializes the network coordinator AO.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_network(app_container_t *self);

/**
 * @brief Initialize the system indicators subsystem (LED & Buzzer).
 *
 * Configures GPIO LED and PWM buzzer adapters and indicator AO.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_indicators(app_container_t *self);

/**
 * @brief Initialize the Industrial HVAC Control Subsystem.
 *
 * Configures default hardware routing, actuator adapter, and HVAC Active Object.
 *
 * @param[in,out] self Container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init_hvac(app_container_t *self);

/**
 * @brief Establish cross-subsystem wiring and provider registrations.
 *
 * Decoupled from adapter creation. Registers storage domain providers
 * and inter-subsystem bindings.
 *
 * @param[in,out] self Initialized container instance.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_wire(app_container_t *self);

/* =========================================================================
 * Public Interface Getters (DIP Encapsulation)
 * ========================================================================= */

/**
 * @brief Get the abstract slave registry port.
 *
 * @param[in] self Container instance.
 * @return Pointer to registry port, or NULL if uninitialized / invalid.
 */
const i_slave_registry_t *app_container_get_registry(const app_container_t *self);

/**
 * @brief Get the abstract Modbus client port for a given bus.
 *
 * @param[in] self Container instance.
 * @param[in] bus Bus index (0 .. MODBUS_BUS_MAX - 1).
 * @return Pointer to Modbus client port, or NULL if uninitialized / invalid bus.
 */
const i_modbus_client_t *app_container_get_modbus_client(const app_container_t *self, uint8_t bus);

/**
 * @brief Get the abstract external relay port.
 *
 * @param[in] self Container instance.
 * @return Pointer to relay port, or NULL if uninitialized / invalid.
 */
const i_relay_t *app_container_get_relay(const app_container_t *self);

/**
 * @brief Get the abstract Ethernet network port.
 *
 * @param[in] self Container instance.
 * @return Pointer to Ethernet network port, or NULL if uninitialized / invalid.
 */
const i_network_t *app_container_get_network_eth(const app_container_t *self);

/**
 * @brief Get the abstract WiFi network port.
 *
 * @param[in] self Container instance.
 * @return Pointer to WiFi network port, or NULL if uninitialized / invalid.
 */
const i_network_t *app_container_get_network_wifi(const app_container_t *self);

/**
 * @brief Get the abstract WiFi control port.
 *
 * @param[in] self Container instance.
 * @return Pointer to WiFi control port, or NULL if uninitialized / invalid.
 */
const i_wifi_control_t *app_container_get_wifi_control(const app_container_t *self);

/**
 * @brief Get the abstract persistent config storage port.
 *
 * @param[in] self Container instance.
 * @return Pointer to storage port, or NULL if uninitialized / invalid.
 */
const i_config_storage_t *app_container_get_storage(const app_container_t *self);

/**
 * @brief Get the abstract digital input and contact alarm port.
 *
 * @param[in] self Container instance.
 * @return Pointer to digital input port, or NULL if uninitialized / invalid.
 */
const i_digital_input_t *app_container_get_digital_inputs(const app_container_t *self);

/**
 * @brief Get the abstract status LED port.
 *
 * @param[in] self Container instance.
 * @return Pointer to LED port, or NULL if uninitialized / invalid.
 */
const i_led_t *app_container_get_led(const app_container_t *self);

/**
 * @brief Get the abstract acoustic buzzer port.
 *
 * @param[in] self Container instance.
 * @return Pointer to buzzer port, or NULL if uninitialized / invalid.
 */
const i_buzzer_t *app_container_get_buzzer(const app_container_t *self);

/**
 * @brief Get the HVAC Active Object coordinator handle.
 *
 * @param[in] self Container instance.
 * @return Pointer to ao_hvac_t, or NULL if uninitialized.
 */
ao_hvac_t *app_container_get_hvac_ao(app_container_t *self);

/**
 * @brief Dynamically update HVAC group configuration at runtime.
 *
 * Updates RAM configuration in ao_hvac immediately and marks storage dirty
 * for asynchronous debounced persistence to EEPROM.
 *
 * @param[in,out] self     Container instance.
 * @param[in]     group_id Group ID (0 .. HVAC_MAX_GROUPS - 1).
 * @param[in]     cfg      New group configuration.
 * @return 0 on success, negative errno on error.
 */
int app_container_set_hvac_group_config(app_container_t *self, uint8_t group_id, const config_hvac_group_t *cfg);

/**
 * @brief Dynamically update complete HVAC configuration at runtime.
 *
 * Updates RAM configuration in ao_hvac immediately and marks storage dirty
 * for asynchronous debounced persistence to EEPROM.
 *
 * @param[in,out] self Container instance.
 * @param[in]     cfg  New HVAC configuration.
 * @return 0 on success, negative errno on error.
 */
int app_container_set_hvac_config(app_container_t *self, const config_hvac_t *cfg);

/**
 * @brief Apply an operational profile (HVAC configuration + Digital Input mappings).
 *
 * Applies the corresponding configuration (hybrid 4-unit or control-only 8-unit)
 * to the HVAC subsystem and re-initializes the digital input aggregator.
 * Also marks the HVAC domain dirty for persistence.
 *
 * @param[in,out] self Container instance.
 * @param[in]     mode Operating mode profile to apply (@see hvac_operation_mode_t).
 * @return 0 on success, negative errno on error.
 */
int app_container_apply_profile(app_container_t *self, hvac_operation_mode_t mode);

/* =========================================================================
 * Full Lifecycle Management
 * ========================================================================= */

/**
 * @brief Sequence orchestrator: create, configure, and wire every subsystem.
 *
 * Calls init_modbus, init_storage, init_network, init_indicators,
 * init_digital_inputs, then composition_root_wire. Idempotent.
 *
 * @param[out] self Container to initialise.
 * @return 0 on success, negative errno on failure.
 */
int composition_root_init(app_container_t *self);

/**
 * @brief Start the Active Objects. Idempotent.
 *
 * @param[in,out] self Container previously initialised.
 * @return 0 on success, -EINVAL when @p self is not initialised.
 */
int composition_root_start(app_container_t *self);

/**
 * @brief Stop the Active Objects with coordinated timeouts. Idempotent.
 *
 * @param[in,out] self Container previously initialised.
 * @return 0 on success, -EINVAL when @p self is not initialised.
 */
int composition_root_stop(app_container_t *self);

/**
 * @brief Stop and deinitialize all Active Objects, terminating their threads.
 *
 * @param[in,out] self Container previously initialised.
 * @return 0 on success, -EINVAL when @p self is NULL.
 */
int composition_root_deinit(app_container_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_DI_COMPOSITION_ROOT_H_ */
