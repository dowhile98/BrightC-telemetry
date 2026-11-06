/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "composition_root.h"
#include "common/config_envelope.h"
#include "common/default_config_profiles.h"
#include "infrastructure/adapters/adapter_di_gpio.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(composition_root, LOG_LEVEL_INF);

/* Compile-time assertions for persistent storage layout (§4.1.C, P1-8) */
BUILD_ASSERT(sizeof(config_hvac_t) + sizeof(config_envelope_header_t) <= 1024 - 32,
             "config_hvac_t envelope exceeds storage slot size (1024 - 32 bytes)");
BUILD_ASSERT(sizeof(net_eth_config_t) + sizeof(config_envelope_header_t) <= 512 - 32,
             "net_eth_config_t envelope exceeds storage slot size (512 - 32 bytes)");
BUILD_ASSERT(sizeof(net_wifi_config_t) + sizeof(config_envelope_header_t) <= 512 - 32,
             "net_wifi_config_t envelope exceeds storage slot size (512 - 32 bytes)");

/* EEPROM capacity check: 32 KB AT24C256C */
#define EEPROM_AT24C256C_CAPACITY_BYTES 32768U
BUILD_ASSERT(0x2800 + 2048 <= EEPROM_AT24C256C_CAPACITY_BYTES,
             "Storage domains exceed physical EEPROM capacity (32KB)");

/* Non-overlapping domain layout checks */
BUILD_ASSERT(0x0000 + 1024 <= 0x0400, "DEVICE domain overlaps MODBUS");
BUILD_ASSERT(0x0400 + 2048 <= 0x0C00, "MODBUS domain overlaps NET_ETH");
BUILD_ASSERT(0x0C00 + 1024 <= 0x1000, "NET_ETH domain overlaps NET_WIFI");
BUILD_ASSERT(0x1000 + 1024 <= 0x1400, "NET_WIFI domain overlaps CALIBRATION");
BUILD_ASSERT(0x1400 + 1024 <= 0x1800, "CALIBRATION domain overlaps SENSOR_CFG");
BUILD_ASSERT(0x1800 + 2048 <= 0x2000, "SENSOR_CFG domain overlaps BASELINE");
BUILD_ASSERT(0x2000 + 2048 <= 0x2800, "BASELINE domain overlaps HVAC");

#ifdef CONFIG_NANOMODBUS
/**
 * Transport configuration of each bus, taken from devicetree.
 *
 * The nodes are declared in app/boards/brightc-controllerv3.overlay:
 * `modbus-rtu-1` -> USART2 (RS485-1) and `modbus-rtu-2` -> USART6 (RS485-2).
 */
static const nmbs_z_serial_transport_config_t k_bus_transport_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_ALIAS(modbus_rtu_1)),
    [MODBUS_BUS_RS485_2] = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_ALIAS(modbus_rtu_2)),
};
#endif

/*
 * Thread stacks defined at file scope to satisfy MPU alignment and guard constraints.
 */
static K_THREAD_STACK_ARRAY_DEFINE(g_ao_stacks, MODBUS_BUS_MAX, AO_MODBUS_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_net_stack, AO_NETWORK_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_store_stack, AO_STORAGE_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_indicator_stack, AO_INDICATOR_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_hvac_stack, AO_HVAC_STACK_SIZE);

static int storage_provider_eth(void *ctx, config_domain_t domain,
                                void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_NET_ETH || out_buf == NULL)
    {
        return -EINVAL;
    }
    net_eth_config_t cfg;
    int err = ao_network_get_eth_config(&app->network.ao, &cfg);
    if (err != 0)
    {
        return err;
    }
    return config_envelope_pack(CONFIG_DOMAIN_NET_ETH, &cfg, sizeof(cfg), out_buf, max_size, out_size);
}

static int storage_provider_wifi(void *ctx, config_domain_t domain,
                                 void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_NET_WIFI || out_buf == NULL)
    {
        return -EINVAL;
    }
    net_wifi_config_t cfg;
    int err = ao_network_get_wifi_config(&app->network.ao, &cfg);
    if (err != 0)
    {
        return err;
    }
    return config_envelope_pack(CONFIG_DOMAIN_NET_WIFI, &cfg, sizeof(cfg), out_buf, max_size, out_size);
}

static int storage_provider_modbus(void *ctx, config_domain_t domain,
                                   void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_MODBUS || out_buf == NULL)
    {
        return -EINVAL;
    }
    return modbus_registry_export(&app->modbus.registry_impl, out_buf, max_size, out_size);
}

static int storage_provider_hvac(void *ctx, config_domain_t domain,
                                 void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_HVAC || out_buf == NULL)
    {
        return -EINVAL;
    }
    config_hvac_t cfg;
    int err = ao_hvac_get_config(&app->hvac.ao, &cfg);
    if (err != 0)
    {
        return err;
    }
    return config_envelope_pack(CONFIG_DOMAIN_HVAC, &cfg, sizeof(cfg), out_buf, max_size, out_size);
}

#if defined(CONFIG_ZTEST)
#include "fake_network.h"
#include "fake_config_storage.h"
#include "fake_led.h"
#include "fake_buzzer.h"
#endif

/* =========================================================================
 * Modular Subsystem Initializers
 * ========================================================================= */

int composition_root_init_modbus(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->modbus.initialized)
    {
        return 0;
    }

    (void)memcpy(self->modbus.bus_cfg, k_default_modbus_bus_cfg, sizeof(self->modbus.bus_cfg));

#ifdef CONFIG_NANOMODBUS
    size_t bound_buses = 0U;

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        int err = adapter_modbus_nmbs_init(&self->modbus.nmbs[bus], &k_bus_transport_cfg[bus]);

        if (err == 0)
        {
            err = adapter_modbus_nmbs_port(&self->modbus.nmbs[bus], &self->modbus.client_port[bus]);
        }

        if (err == 0)
        {
            err = i_modbus_client_get_bus_cfg(&self->modbus.client_port[bus], &self->modbus.bus_cfg[bus]);
        }

        if (err != 0)
        {
            LOG_ERR("bus %u: transport unavailable (%d), continuing without it", bus, err);
            self->modbus.client_port[bus].api = NULL;
            self->modbus.client_port[bus].impl = NULL;
            continue;
        }

        bound_buses++;
    }

    if (bound_buses == 0U)
    {
        LOG_ERR("no Modbus transport could be brought up");
        return -ENODEV;
    }
#endif

    int err = modbus_registry_init(&self->modbus.registry_impl, self->modbus.bus_cfg);
    if (err != 0)
    {
        LOG_ERR("registry init failed (%d)", err);
        return err;
    }

    err = modbus_registry_port(&self->modbus.registry_impl, &self->modbus.registry_port);
    if (err != 0)
    {
        LOG_ERR("registry port binding failed (%d)", err);
        return err;
    }

    size_t slave_count = 0U;
    const modbus_slave_descriptor_t *default_slaves = default_config_get_modbus_slaves(&slave_count);
    for (size_t i = 0U; i < slave_count; i++)
    {
        err = modbus_registry_register(&self->modbus.registry_impl, &default_slaves[i]);
        if (err != 0)
        {
            LOG_ERR("registering device %u failed (%d)", default_slaves[i].device_id, err);
            return err;
        }
    }

    if (i_config_storage_is_bound(&self->storage.port))
    {
        uint8_t modbus_blob[MODBUS_REGISTRY_EXPORT_SIZE_MAX];
        size_t loaded_size = 0U;
        int load_res = i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_MODBUS,
                                             modbus_blob, sizeof(modbus_blob),
                                             &loaded_size);
        if (load_res == 0 && loaded_size > 0U)
        {
            int import_res = modbus_registry_import(&self->modbus.registry_impl, modbus_blob, loaded_size);
            if (import_res == 0)
            {
                LOG_INF("Loaded Modbus registry config from storage (%zu bytes)", loaded_size);
                for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
                {
                    modbus_bus_cfg_t bcfg;
                    if (modbus_registry_get_bus_cfg(&self->modbus.registry_impl, bus, &bcfg) == 0)
                    {
                        self->modbus.bus_cfg[bus] = bcfg;
                        if (self->modbus.client_port[bus].api != NULL)
                        {
                            (void)i_modbus_client_set_bus_cfg(&self->modbus.client_port[bus], &bcfg);
                        }
                    }
                }
            }
            else
            {
                LOG_WRN("Failed to import Modbus registry config (%d), re-persisting defaults", import_res);
                size_t export_size = 0U;
                if (modbus_registry_export(&self->modbus.registry_impl, modbus_blob, sizeof(modbus_blob), &export_size) == 0)
                {
                    (void)i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_MODBUS, modbus_blob, export_size);
                }
            }
        }
        else
        {
            LOG_INF("No valid Modbus config in storage (%d), persisting defaults", load_res);
            size_t export_size = 0U;
            if (modbus_registry_export(&self->modbus.registry_impl, modbus_blob, sizeof(modbus_blob), &export_size) == 0)
            {
                (void)i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_MODBUS, modbus_blob, export_size);
            }
        }
    }

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        k_msgq_init(&self->modbus.ao_queue[bus], self->modbus.ao_queue_buf[bus],
                    sizeof(modbus_event_t), AO_MODBUS_QUEUE_DEPTH);

        err = ao_modbus_init(&self->modbus.ao[bus], bus, &self->modbus.registry_port,
                             &self->modbus.client_port[bus], &self->modbus.ao_queue[bus],
                             g_ao_stacks[bus], AO_MODBUS_STACK_SIZE);
        if (err != 0)
        {
            LOG_ERR("bus %u: active object init failed (%d)", bus, err);
            return err;
        }
    }

    uint8_t waveshare_bus = (uint8_t)MODBUS_BUS_RS485_1;
    uint8_t waveshare_addr = 4U;
    uint16_t waveshare_timeout_ms = 300U;

    modbus_slave_descriptor_t ws_desc;
    if (modbus_registry_find_by_type(&self->modbus.registry_impl,
                                     MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
                                     &ws_desc) == 0)
    {
        waveshare_bus = ws_desc.bus_id;
        waveshare_addr = ws_desc.slave_addr;
        waveshare_timeout_ms = ws_desc.timeout_ms;
    }

    if (waveshare_bus >= (uint8_t)MODBUS_BUS_MAX)
    {
        waveshare_bus = (uint8_t)MODBUS_BUS_RS485_1;
    }

    err = adapter_relay_modbus_init(&self->modbus.waveshare_relay,
                                    &self->modbus.ao[waveshare_bus],
                                    waveshare_addr,
                                    (int32_t)waveshare_timeout_ms);
    if (err != 0)
    {
        LOG_ERR("relay adapter init failed (%d)", err);
        return err;
    }

    err = adapter_relay_modbus_port(&self->modbus.waveshare_relay, &self->modbus.relay_ext_port);
    if (err != 0)
    {
        LOG_ERR("relay port binding failed (%d)", err);
        return err;
    }

    self->modbus.initialized = true;
    return 0;
}

int composition_root_init_storage(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->storage.initialized)
    {
        return 0;
    }

#if defined(CONFIG_ZTEST)
    if (!i_config_storage_is_bound(&self->storage.port))
    {
        self->storage.port = fake_config_storage_get_port();
    }
#else
    const struct device *eeprom_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
    eeprom_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom0));
#endif
    if (eeprom_dev != NULL)
    {
        int store_err = adapter_storage_microconf_init(&self->storage.storage_adapter, eeprom_dev);
        if (store_err == 0)
        {
            self->storage.port = adapter_storage_microconf_port(&self->storage.storage_adapter);
        }
        else
        {
            LOG_WRN("adapter_storage_microconf_init failed (%d)", store_err);
        }
    }
    else
    {
        LOG_WRN("eeprom0 devicetree node not found");
    }
#endif

    if (!i_config_storage_is_bound(&self->storage.port))
    {
        LOG_WRN("Persistent storage is not bound; storage subsystem operating in degraded mode");
    }

    k_msgq_init(&self->storage.queue, self->storage.queue_buf,
                sizeof(ao_storage_event_t), AO_STORAGE_QUEUE_DEPTH);

    int err = ao_storage_init(&self->storage.ao, &self->storage.port,
                              &self->storage.queue, s_ao_store_stack,
                              K_THREAD_STACK_SIZEOF(s_ao_store_stack));
    if (err != 0)
    {
        LOG_ERR("storage active object init failed (%d)", err);
        return err;
    }

    self->storage.initialized = true;
    return 0;
}

int composition_root_init_network(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->network.initialized)
    {
        return 0;
    }

    k_msgq_init(&self->network.queue, self->network.queue_buf, sizeof(ao_network_event_t),
                AO_NETWORK_QUEUE_DEPTH);

#if defined(CONFIG_ZTEST)
    if (!i_network_is_bound(&self->network.eth_port))
    {
        self->network.eth_port = fake_network_get_eth_port();
    }
    if (!i_network_is_bound(&self->network.wifi_port))
    {
        self->network.wifi_port = fake_network_get_wifi_port();
    }
    if (!i_wifi_control_is_bound(&self->network.wifi_ctrl_port))
    {
        self->network.wifi_ctrl_port = fake_network_get_wifi_control_port();
    }
#else
    const struct device *eth_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mac))
    eth_dev = DEVICE_DT_GET(DT_NODELABEL(mac));
#endif
    int eth_err = adapter_eth_stm32_init(&self->network.eth_adapter, eth_dev);
    if (eth_err == 0)
    {
        self->network.eth_port = adapter_eth_stm32_port(&self->network.eth_adapter);
    }
    else
    {
        LOG_WRN("adapter_eth_stm32_init failed (%d)", eth_err);
    }

    const struct device *wifi_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(esp_hosted_mcu_wifi))
    wifi_dev = DEVICE_DT_GET(DT_NODELABEL(esp_hosted_mcu_wifi));
#endif
    int wifi_err = adapter_wifi_esp_hosted_init(&self->network.wifi_adapter, wifi_dev);
    if (wifi_err == 0)
    {
        self->network.wifi_port = adapter_wifi_esp_hosted_port(&self->network.wifi_adapter);
        self->network.wifi_ctrl_port = adapter_wifi_esp_hosted_wifi_control_port(&self->network.wifi_adapter);
    }
    else
    {
        LOG_WRN("adapter_wifi_esp_hosted_init failed (%d)", wifi_err);
    }
#endif

    /* Synchronous boot read from storage for network configurations */
    net_eth_config_t loaded_eth_cfg;
    const net_eth_config_t *eth_cfg_ptr = default_config_get_net_eth_cfg();
    uint8_t eth_buf[sizeof(config_envelope_header_t) + sizeof(net_eth_config_t)];
    size_t loaded_eth_size = 0;
    int eth_load_res = -1;

    if (i_config_storage_is_bound(&self->storage.port))
    {
        eth_load_res = i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_NET_ETH,
                                             eth_buf, sizeof(eth_buf),
                                             &loaded_eth_size);
        if (eth_load_res == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, eth_buf, loaded_eth_size,
                                   &loaded_eth_cfg, sizeof(loaded_eth_cfg), NULL) == 0 &&
            net_eth_config_is_valid(&loaded_eth_cfg))
        {
            LOG_INF("Loaded Ethernet config from storage");
            eth_cfg_ptr = &loaded_eth_cfg;
        }
        else
        {
            LOG_INF("No valid Ethernet config in storage (%d), applying defaults", eth_load_res);
            size_t pack_len = 0;
            if (config_envelope_pack(CONFIG_DOMAIN_NET_ETH, eth_cfg_ptr, sizeof(*eth_cfg_ptr),
                                     eth_buf, sizeof(eth_buf), &pack_len) == 0)
            {
                (void)i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_NET_ETH,
                                            eth_buf, pack_len);
            }
        }
    }

    net_wifi_config_t loaded_wifi_cfg;
    const net_wifi_config_t *wifi_cfg_ptr = default_config_get_net_wifi_cfg();
    uint8_t wifi_buf[sizeof(config_envelope_header_t) + sizeof(net_wifi_config_t)];
    size_t loaded_wifi_size = 0;
    int wifi_load_res = -1;

    if (i_config_storage_is_bound(&self->storage.port))
    {
        wifi_load_res = i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_NET_WIFI,
                                              wifi_buf, sizeof(wifi_buf),
                                              &loaded_wifi_size);
        if (wifi_load_res == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_NET_WIFI, wifi_buf, loaded_wifi_size,
                                   &loaded_wifi_cfg, sizeof(loaded_wifi_cfg), NULL) == 0 &&
            net_wifi_config_is_valid(&loaded_wifi_cfg))
        {
            LOG_INF("Loaded Wi-Fi config from storage");
            wifi_cfg_ptr = &loaded_wifi_cfg;
        }
        else
        {
            LOG_INF("No valid Wi-Fi config in storage (%d), applying defaults", wifi_load_res);
            size_t pack_len = 0;
            if (config_envelope_pack(CONFIG_DOMAIN_NET_WIFI, wifi_cfg_ptr, sizeof(*wifi_cfg_ptr),
                                     wifi_buf, sizeof(wifi_buf), &pack_len) == 0)
            {
                (void)i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_NET_WIFI,
                                            wifi_buf, pack_len);
            }
        }
    }

    int err = ao_network_init(&self->network.ao, &self->network.eth_port, &self->network.wifi_port,
                              eth_cfg_ptr, wifi_cfg_ptr,
                              &self->network.queue, s_ao_net_stack,
                              K_THREAD_STACK_SIZEOF(s_ao_net_stack));
    if (err != 0)
    {
        LOG_ERR("network active object init failed (%d)", err);
        return err;
    }

    self->network.initialized = true;
    return 0;
}

int composition_root_init_indicators(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->indicators.initialized)
    {
        return 0;
    }

    k_msgq_init(&self->indicators.queue, self->indicators.queue_buf,
                sizeof(ao_indicator_event_t), AO_INDICATOR_QUEUE_DEPTH);

#if defined(CONFIG_ZTEST)
    if (!i_led_is_bound(&self->indicators.led_port))
    {
        self->indicators.led_port = fake_led_get_port();
    }
    if (!i_buzzer_is_bound(&self->indicators.buzzer_port))
    {
        self->indicators.buzzer_port = fake_buzzer_get_port();
    }
#else
#if DT_NODE_EXISTS(DT_ALIAS(led0))
    static const struct gpio_dt_spec s_led_spec = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#elif DT_NODE_EXISTS(DT_NODELABEL(user_led))
    static const struct gpio_dt_spec s_led_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(user_led), gpios);
#else
    static const struct gpio_dt_spec s_led_spec = {0};
#endif

    int led_err = adapter_led_gpio_init(&self->indicators.led_adapter, &s_led_spec);
    if (led_err == 0)
    {
        (void)adapter_led_gpio_port(&self->indicators.led_adapter, &self->indicators.led_port);
    }
    else
    {
        LOG_WRN("adapter_led_gpio_init failed (%d)", led_err);
    }

#if DT_NODE_EXISTS(DT_NODELABEL(buzzer_pwm))
    static const struct pwm_dt_spec s_buzzer_spec = PWM_DT_SPEC_GET(DT_NODELABEL(buzzer_pwm));
#else
    static const struct pwm_dt_spec s_buzzer_spec = {0};
#endif

    int buzzer_err = adapter_buzzer_pwm_init(&self->indicators.buzzer_adapter, &s_buzzer_spec);
    if (buzzer_err == 0)
    {
        (void)adapter_buzzer_pwm_port(&self->indicators.buzzer_adapter, &self->indicators.buzzer_port);
    }
    else
    {
        LOG_WRN("adapter_buzzer_pwm_init failed (%d)", buzzer_err);
    }
#endif

    int err = ao_system_indicator_init(&self->indicators.ao, &self->indicators.led_port,
                                       &self->indicators.buzzer_port,
                                       &self->indicators.queue, s_ao_indicator_stack,
                                       K_THREAD_STACK_SIZEOF(s_ao_indicator_stack));
    if (err != 0)
    {
        LOG_ERR("ao_system_indicator_init failed (%d)", err);
        return err;
    }

    self->indicators.initialized = true;
    return 0;
}

#define k_default_di_channels k_default_di_channels_hybrid

int composition_root_init_digital_inputs(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->di.initialized)
    {
        return 0;
    }

    k_msgq_init(&self->di.queue, self->di.queue_buf,
                sizeof(ao_di_event_t), AO_DI_QUEUE_DEPTH);

#if !defined(CONFIG_ZTEST)
    const struct device *mcp_dev0 = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mcp23s17_0))
    mcp_dev0 = DEVICE_DT_GET(DT_NODELABEL(mcp23s17_0));
#endif

    const struct device *mcp_dev1 = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mcp23s17_1))
    mcp_dev1 = DEVICE_DT_GET(DT_NODELABEL(mcp23s17_1));
#endif

    int adp_err = adapter_di_gpio_init(&self->di.di_adapter, mcp_dev0, mcp_dev1);
    if (adp_err != 0)
    {
        LOG_WRN("adapter_di_gpio_init failed (%d)", adp_err);
    }

    size_t di_count = 0U;
    const di_channel_config_t *di_cfg = default_config_get_di_profile(HVAC_MODE_HYBRID_FULL, &di_count);

    int err = di_aggregator_init(&self->di.aggregator, di_cfg,
                                 di_count,
                                 adapter_di_gpio_read, &self->di.di_adapter);
    if (err != 0)
    {
        LOG_ERR("di_aggregator_init failed (%d)", err);
        return err;
    }
#else
    size_t di_count = 0U;
    const di_channel_config_t *di_cfg = default_config_get_di_profile(HVAC_MODE_HYBRID_FULL, &di_count);

    int err = di_aggregator_init(&self->di.aggregator, di_cfg,
                                 di_count,
                                 NULL, NULL);
    if (err != 0)
    {
        LOG_ERR("di_aggregator_init failed (%d)", err);
        return err;
    }
#endif

    self->di.port = di_aggregator_port(&self->di.aggregator);

    err = ao_digital_input_init(&self->di.ao, &self->di.aggregator, &self->di.queue,
                                s_ao_di_stack, K_THREAD_STACK_SIZEOF(s_ao_di_stack));
    if (err != 0)
    {
        LOG_ERR("ao_digital_input_init failed (%d)", err);
        return err;
    }

    self->di.initialized = true;
    return 0;
}

int composition_root_init_hvac(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->hvac.initialized)
    {
        return 0;
    }

    /* 1. Synchronous boot read from storage for HVAC configuration */
    config_hvac_t loaded_hvac_cfg;
    const config_hvac_t *hvac_cfg_ptr = default_config_get_hvac_profile(HVAC_MODE_HYBRID_FULL);
    uint8_t hvac_buf[sizeof(config_envelope_header_t) + sizeof(config_hvac_t)];
    size_t loaded_hvac_size = 0;
    int hvac_load_res = -1;

    if (i_config_storage_is_bound(&self->storage.port))
    {
        hvac_load_res = i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_HVAC,
                                              hvac_buf, sizeof(hvac_buf),
                                              &loaded_hvac_size);
        if (hvac_load_res == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_HVAC, hvac_buf, loaded_hvac_size,
                                   &loaded_hvac_cfg, sizeof(loaded_hvac_cfg), NULL) == 0 &&
            config_hvac_is_valid(&loaded_hvac_cfg))
        {
            LOG_INF("Loaded HVAC config from storage");
            hvac_cfg_ptr = &loaded_hvac_cfg;
        }
        else
        {
            LOG_INF("No valid HVAC config in storage (%d), applying defaults", hvac_load_res);
            size_t pack_len = 0;
            if (config_envelope_pack(CONFIG_DOMAIN_HVAC, hvac_cfg_ptr, sizeof(*hvac_cfg_ptr),
                                     hvac_buf, sizeof(hvac_buf), &pack_len) == 0)
            {
                (void)i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_HVAC,
                                            hvac_buf, pack_len);
            }
        }
    }
    (void)memcpy(&self->hvac.config, hvac_cfg_ptr, sizeof(config_hvac_t));

    /* 2. Bind remote relay port to waveshare relay */
    i_relay_t remote_relay;
    int err = adapter_relay_modbus_port(&self->modbus.waveshare_relay, &remote_relay);
    if (err != 0)
    {
        LOG_WRN("hvac: failed to obtain waveshare relay port: %d", err);
    }

    /* 3. Initialize local GPIO relay adapter (BrightC internal relays) */
#if !defined(CONFIG_ZTEST)
    const struct device *mcp_dev2 = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mcp23s17_2))
    mcp_dev2 = DEVICE_DT_GET(DT_NODELABEL(mcp23s17_2));
#endif

    const struct device *mcp_dev3 = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mcp23s17_3))
    mcp_dev3 = DEVICE_DT_GET(DT_NODELABEL(mcp23s17_3));
#endif

    int local_relay_err = adapter_relay_gpio_init(&self->hvac.local_relay_adapter, mcp_dev2, mcp_dev3);
    if (local_relay_err != 0)
    {
        LOG_WRN("adapter_relay_gpio_init failed (%d)", local_relay_err);
    }
#else
    int local_relay_err = adapter_relay_gpio_init(&self->hvac.local_relay_adapter, NULL, NULL);
    if (local_relay_err != 0)
    {
        LOG_WRN("adapter_relay_gpio_init failed (%d)", local_relay_err);
    }
#endif

    local_relay_err = adapter_relay_gpio_port(&self->hvac.local_relay_adapter, &self->hvac.local_relay_port);
    if (local_relay_err != 0)
    {
        LOG_WRN("adapter_relay_gpio_port failed (%d)", local_relay_err);
    }
    self->modbus.relay_int_port = self->hvac.local_relay_port;

    /* 4. Initialize actuator adapter */
    err = adapter_hvac_actuator_init(&self->hvac.actuator_adapter,
                                     self->hvac.config.units,
                                     HVAC_MAX_UNITS_TOTAL,
                                     &remote_relay,
                                     &self->hvac.local_relay_port);
    if (err != 0)
    {
        LOG_ERR("adapter_hvac_actuator_init failed (%d)", err);
        return err;
    }
    self->hvac.actuator_port = adapter_hvac_actuator_port(&self->hvac.actuator_adapter);

    /* 4. Initialize message queue and Active Object */
    k_msgq_init(&self->hvac.queue, (char *)self->hvac.queue_buf,
                sizeof(ao_hvac_event_t), AO_HVAC_QUEUE_DEPTH);

    err = ao_hvac_init(&self->hvac.ao,
                       &self->hvac.config,
                       &self->hvac.actuator_port,
                       &self->hvac.queue,
                       s_ao_hvac_stack,
                       K_THREAD_STACK_SIZEOF(s_ao_hvac_stack));
    if (err != 0)
    {
        LOG_ERR("ao_hvac_init failed (%d)", err);
        return err;
    }

    ao_hvac_set_actuator_adapter(&self->hvac.ao, &self->hvac.actuator_adapter);

    self->hvac.initialized = true;
    return 0;
}

static void on_modbus_mbsl32di_reading(void *ctx, uint8_t device_id, uint32_t raw_inputs)
{
    ARG_UNUSED(device_id);
    app_container_t *self = (app_container_t *)ctx;
    if (self == NULL)
    {
        return;
    }
#if !defined(CONFIG_ZTEST)
    adapter_di_gpio_set_modbus_raw(&self->di.di_adapter, raw_inputs, true);
#endif
}

int composition_root_wire(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->storage.initialized)
    {
        (void)ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_NET_ETH,
                                           storage_provider_eth, self);
        (void)ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_NET_WIFI,
                                           storage_provider_wifi, self);
        (void)ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_MODBUS,
                                           storage_provider_modbus, self);
        if (self->hvac.initialized)
        {
            (void)ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_HVAC,
                                               storage_provider_hvac, self);
        }
    }

    if (self->network.initialized)
    {
#if !defined(CONFIG_ZTEST)
        self->network.wifi_ctrl_port = adapter_wifi_esp_hosted_wifi_control_port(&self->network.wifi_adapter);
#else
        if (!i_wifi_control_is_bound(&self->network.wifi_ctrl_port))
        {
            self->network.wifi_ctrl_port = fake_network_get_wifi_control_port();
        }
#endif
    }

    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            ao_modbus_set_mbsl32di_callback(&self->modbus.ao[bus], on_modbus_mbsl32di_reading, self);
        }
    }

    return 0;
}

/* =========================================================================
 * Full Lifecycle Management
 * ========================================================================= */

int composition_root_init(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->is_initialized)
    {
        return 0;
    }

    (void)memset(self, 0, sizeof(*self));

    int err = composition_root_init_storage(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_modbus(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_network(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_indicators(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_digital_inputs(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_hvac(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_wire(self);
    if (err != 0)
    {
        return err;
    }

    self->is_initialized = true;
    return 0;
}

int composition_root_start(app_container_t *self)
{
    if (self == NULL || !self->is_initialized)
    {
        return -EINVAL;
    }

    if (self->is_running)
    {
        return 0;
    }

    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            int err = ao_modbus_start(&self->modbus.ao[bus]);

            if (err != 0 && err != -EALREADY)
            {
                LOG_ERR("bus %u: active object start failed (%d)", bus, err);
                return err;
            }
        }
    }

    if (self->storage.initialized)
    {
        int err = ao_storage_start(&self->storage.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("storage active object start failed (%d)", err);
            return err;
        }
    }

    if (self->network.initialized)
    {
        int err = ao_network_start(&self->network.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("network active object start failed (%d)", err);
            return err;
        }
    }

    if (self->indicators.initialized)
    {
        int err = ao_system_indicator_start(&self->indicators.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("indicator active object start failed (%d)", err);
            return err;
        }

        /* Trigger startup chime on system boot */
        (void)ao_system_indicator_trigger_startup_chime(&self->indicators.ao);
    }

    if (self->di.initialized)
    {
        int err = ao_digital_input_start(&self->di.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("digital input active object start failed (%d)", err);
            return err;
        }
    }

    if (self->hvac.initialized)
    {
        int err = ao_hvac_start(&self->hvac.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("hvac active object start failed (%d)", err);
            return err;
        }
    }

    self->is_running = true;
    return 0;
}

int composition_root_stop(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    /* 1. Stop Modbus polling threads (wait up to 1000 ms) */
    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            (void)ao_modbus_stop(&self->modbus.ao[bus]);
            (void)ao_modbus_wait_stopped(&self->modbus.ao[bus], K_MSEC(1000));
        }
    }

    /* 2. Stop digital inputs (wait up to 200 ms) */
    if (self->di.initialized)
    {
        (void)ao_digital_input_stop(&self->di.ao);
        (void)ao_digital_input_wait_stopped(&self->di.ao, K_MSEC(200));
    }

    /* 3. Stop storage coordinator (wait up to 2000 ms - flushes pending changes) */
    if (self->storage.initialized)
    {
        (void)ao_storage_stop(&self->storage.ao);
        (void)ao_storage_wait_stopped(&self->storage.ao, K_MSEC(2000));
    }

    /* 4. Stop network coordinator (wait up to 2000 ms) */
    if (self->network.initialized)
    {
        (void)ao_network_stop(&self->network.ao);
        (void)ao_network_wait_stopped(&self->network.ao, K_MSEC(2000));
    }

    /* 5. Stop system indicators (wait up to 200 ms) */
    if (self->indicators.initialized)
    {
        (void)ao_system_indicator_stop(&self->indicators.ao);
        (void)ao_system_indicator_wait_stopped(&self->indicators.ao, K_MSEC(200));
    }

    /* 6. Stop HVAC coordinator (wait up to 1000 ms) */
    if (self->hvac.initialized)
    {
        (void)ao_hvac_stop(&self->hvac.ao);
        (void)ao_hvac_wait_stopped(&self->hvac.ao, K_MSEC(1000));
    }

    self->is_running = false;
    return 0;
}

int composition_root_deinit(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (!self->is_initialized)
    {
        return 0;
    }

    /* Stop all Active Objects gracefully first */
    (void)composition_root_stop(self);

    /* Deinitialize subsystems in reverse order of initialization */
    if (self->hvac.initialized)
    {
        (void)ao_hvac_deinit(&self->hvac.ao);
        self->hvac.initialized = false;
    }

    if (self->di.initialized)
    {
        (void)ao_digital_input_deinit(&self->di.ao);
        self->di.initialized = false;
    }

    if (self->indicators.initialized)
    {
        (void)ao_system_indicator_deinit(&self->indicators.ao);
        self->indicators.initialized = false;
    }

    if (self->network.initialized)
    {
        (void)ao_network_deinit(&self->network.ao);
        self->network.initialized = false;
    }

    if (self->storage.initialized)
    {
        (void)ao_storage_deinit(&self->storage.ao);
        self->storage.initialized = false;
    }

    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            (void)ao_modbus_deinit(&self->modbus.ao[bus]);
        }
        self->modbus.initialized = false;
    }

    self->is_initialized = false;
    self->is_running = false;
    return 0;
}

/* =========================================================================
 * Public Interface Getters (DIP Encapsulation)
 * ========================================================================= */

const i_slave_registry_t *app_container_get_registry(const app_container_t *self)
{
    if (self == NULL || !self->modbus.initialized)
    {
        return NULL;
    }
    return &self->modbus.registry_port;
}

const i_modbus_client_t *app_container_get_modbus_client(const app_container_t *self, uint8_t bus)
{
    if (self == NULL || !self->modbus.initialized || bus >= (uint8_t)MODBUS_BUS_MAX)
    {
        return NULL;
    }
    return &self->modbus.client_port[bus];
}

const i_relay_t *app_container_get_relay(const app_container_t *self)
{
    if (self == NULL || !self->modbus.initialized)
    {
        return NULL;
    }
    return &self->modbus.relay_ext_port;
}

const i_network_t *app_container_get_network_eth(const app_container_t *self)
{
    if (self == NULL || !self->network.initialized)
    {
        return NULL;
    }
    return &self->network.eth_port;
}

const i_network_t *app_container_get_network_wifi(const app_container_t *self)
{
    if (self == NULL || !self->network.initialized)
    {
        return NULL;
    }
    return &self->network.wifi_port;
}

const i_wifi_control_t *app_container_get_wifi_control(const app_container_t *self)
{
    if (self == NULL || !self->network.initialized)
    {
        return NULL;
    }
    return &self->network.wifi_ctrl_port;
}

const i_config_storage_t *app_container_get_storage(const app_container_t *self)
{
    if (self == NULL || !self->storage.initialized)
    {
        return NULL;
    }
    return &self->storage.port;
}

const i_digital_input_t *app_container_get_digital_inputs(const app_container_t *self)
{
    if (self == NULL || !self->di.initialized)
    {
        return NULL;
    }
    return &self->di.port;
}

const i_led_t *app_container_get_led(const app_container_t *self)
{
    if (self == NULL || !self->indicators.initialized)
    {
        return NULL;
    }
    return &self->indicators.led_port;
}

const i_buzzer_t *app_container_get_buzzer(const app_container_t *self)
{
    if (self == NULL || !self->indicators.initialized)
    {
        return NULL;
    }
    return &self->indicators.buzzer_port;
}

ao_hvac_t *app_container_get_hvac_ao(app_container_t *self)
{
    if (self == NULL || !self->hvac.initialized)
    {
        return NULL;
    }
    return &self->hvac.ao;
}

int app_container_set_hvac_group_config(app_container_t *self, uint8_t group_id, const config_hvac_group_t *cfg)
{
    if (self == NULL || group_id >= HVAC_MAX_GROUPS || cfg == NULL)
    {
        return -EINVAL;
    }

    if (!config_hvac_group_is_valid(cfg))
    {
        return -EINVAL;
    }

    if (self->hvac.initialized)
    {
        int err = ao_hvac_set_group_config(&self->hvac.ao, group_id, cfg);
        if (err != 0)
        {
            return err;
        }
    }

    self->hvac.config.groups[group_id] = *cfg;

    if (self->storage.initialized)
    {
        (void)ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_HVAC, (uint16_t)group_id);
    }

    return 0;
}

int app_container_set_hvac_config(app_container_t *self, const config_hvac_t *cfg)
{
    if (self == NULL || cfg == NULL)
    {
        return -EINVAL;
    }

    if (!config_hvac_is_valid(cfg))
    {
        return -EINVAL;
    }

    if (self->hvac.initialized)
    {
        for (uint8_t u = 0U; u < (uint8_t)HVAC_MAX_UNITS_TOTAL; u++)
        {
            (void)adapter_hvac_actuator_set_hw_config(&self->hvac.actuator_adapter, u, &cfg->units[u]);
        }

        int err = ao_hvac_apply_config(&self->hvac.ao, cfg);
        if (err != 0)
        {
            return err;
        }
    }

    self->hvac.config = *cfg;

    if (self->storage.initialized)
    {
        (void)ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_HVAC, 0U);
    }

    return 0;
}

int app_container_apply_profile(app_container_t *self, hvac_operation_mode_t mode)
{
    if (self == NULL || !hvac_operation_mode_is_valid((uint8_t)mode))
    {
        return -EINVAL;
    }

    const config_hvac_t *hvac_cfg = default_config_get_hvac_profile(mode);
    size_t di_count = 0;
    const di_channel_config_t *di_cfg = default_config_get_di_profile(mode, &di_count);

    if (hvac_cfg == NULL || di_cfg == NULL)
    {
        return -EINVAL;
    }

    int err = app_container_set_hvac_config(self, hvac_cfg);
    if (err != 0)
    {
        return err;
    }

    if (self->di.initialized)
    {
        err = di_aggregator_reconfigure(&self->di.aggregator, di_cfg, di_count);
        if (err != 0)
        {
            return err;
        }

        self->di.port = di_aggregator_port(&self->di.aggregator);
    }

    return 0;
}

