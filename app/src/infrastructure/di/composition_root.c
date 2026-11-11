/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "composition_root.h"
#include "application/services/app_facade.h"
#include "application/active_objects/ao_sensor_matrix.h"
#include "application/zbus/sensor_channels.h"
#include "application/zbus/digital_input_channels.h"
#include "application/zbus/hvac_channels.h"
#include "application/zbus/indicator_channels.h"
#include "application/zbus/alarm_channels.h"
#include "common/config_envelope.h"
#include "common/app_defaults.h"
#include "common/modbus_device_table.h"
#include "common/hardware_pin_map.h"
#include "common/sensor_hardware_map.h"
#include "domain/services/cwt_tm16pt_codec.h"
#include "domain/services/sensor_matrix_mapper.h"
#include "infrastructure/adapters/adapter_di_gpio.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(composition_root, LOG_LEVEL_INF);

/* Compile-time assertions for persistent storage layout (5 domains per ADR-6) */
BUILD_ASSERT(sizeof(config_hvac_t) + sizeof(config_envelope_header_t) <= 576 - 32,
             "config_hvac_t envelope exceeds storage slot size (576 - 32 bytes)");
BUILD_ASSERT(sizeof(net_eth_config_t) + sizeof(config_envelope_header_t) <= 64 - 32,
             "net_eth_config_t envelope exceeds storage slot size (64 - 32 bytes)");
BUILD_ASSERT(sizeof(net_wifi_config_t) + sizeof(config_envelope_header_t) <= 320 - 32,
             "net_wifi_config_t envelope exceeds storage slot size (320 - 32 bytes)");
BUILD_ASSERT(sizeof(config_device_t) + sizeof(config_envelope_header_t) <= 128 - 32,
             "config_device_t envelope exceeds storage slot size (128 - 32 bytes)");
BUILD_ASSERT(sizeof(config_calibration_t) + sizeof(config_envelope_header_t) <= 192 - 32,
             "config_calibration_t envelope exceeds storage slot size (192 - 32 bytes)");

/* EEPROM capacity check: 32 KB AT24C256C */
#define EEPROM_AT24C256C_CAPACITY_BYTES 32768U
BUILD_ASSERT(0x0880 + 384 <= EEPROM_AT24C256C_CAPACITY_BYTES,
             "Storage domains exceed physical EEPROM capacity (32KB)");

/* Non-overlapping domain layout checks (ADR-6) */
BUILD_ASSERT(0x0000 + 256 <= 0x0100, "DEVICE domain overlaps NET_ETH");
BUILD_ASSERT(0x0100 + 128 <= 0x0180, "NET_ETH domain overlaps NET_WIFI");
BUILD_ASSERT(0x0180 + 640 <= 0x0400, "NET_WIFI domain overlaps HVAC");
BUILD_ASSERT(0x0400 + 1152 <= 0x0880, "HVAC domain overlaps CALIBRATION");

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
static K_THREAD_STACK_DEFINE(s_ao_sensor_stack, AO_SENSOR_MATRIX_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_alarm_stack, CONFIG_AO_ALARM_MANAGER_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_thermal_energy_stack, AO_THERMAL_ENERGY_STACK_SIZE);

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

static int storage_provider_device(void *ctx, config_domain_t domain,
                                   void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *self = (app_container_t *)ctx;
    if (domain != CONFIG_DOMAIN_DEVICE || out_buf == NULL || self == NULL)
    {
        return -EINVAL;
    }
    return config_envelope_pack(CONFIG_DOMAIN_DEVICE, &self->device_cfg, sizeof(self->device_cfg), out_buf, max_size, out_size);
}

static int storage_provider_calibration(void *ctx, config_domain_t domain,
                                        void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *self = (app_container_t *)ctx;
    if (domain != CONFIG_DOMAIN_CALIBRATION || out_buf == NULL || self == NULL)
    {
        return -EINVAL;
    }
    return config_envelope_pack(CONFIG_DOMAIN_CALIBRATION, &self->calibration_cfg, sizeof(self->calibration_cfg), out_buf, max_size, out_size);
}

static int storage_provider_thermal_site(void *ctx, config_domain_t domain,
                                         void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *self = (app_container_t *)ctx;
    if (domain != CONFIG_DOMAIN_THERMAL_SITE || out_buf == NULL || self == NULL)
    {
        return -EINVAL;
    }
    return config_envelope_pack(CONFIG_DOMAIN_THERMAL_SITE, &self->thermal_site_cfg,
                                sizeof(self->thermal_site_cfg), out_buf, max_size, out_size);
}

static int storage_provider_thermal_baseline(void *ctx, config_domain_t domain,
                                             void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *self = (app_container_t *)ctx;
    if (domain != CONFIG_DOMAIN_THERMAL_BASELINE || out_buf == NULL || self == NULL)
    {
        return -EINVAL;
    }
    return config_envelope_pack(CONFIG_DOMAIN_THERMAL_BASELINE, &self->thermal_baseline_cfg,
                                sizeof(self->thermal_baseline_cfg), out_buf, max_size, out_size);
}



#if defined(CONFIG_ZTEST)
#include "fake_network.h"
#include "fake_config_storage.h"
#include "fake_led.h"
#include "fake_buzzer.h"
#include "fake_sensor_matrix.h"
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

    if (!self->storage.initialized)
    {
        LOG_ERR("storage not initialized; boot sequence requires storage before modbus");
        return -EAGAIN;
    }

    (void)memcpy(self->modbus.bus_cfg, k_brightc_modbus_bus_table, sizeof(self->modbus.bus_cfg));

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
    const modbus_slave_descriptor_t *default_slaves = modbus_device_table_get_slaves(&slave_count);
    hvac_operation_mode_t op_mode = (hvac_operation_mode_t)self->hvac.config.operation_mode;
    uint8_t total_units = self->hvac.config.total_units;

    for (size_t i = 0U; i < slave_count; i++)
    {
        uint8_t dev_type = default_slaves[i].dev_type;

        /* Filter slaves based on persistent operational mode and unit count */
        if (op_mode == HVAC_MODE_TELEMETRY_ONLY)
        {
            if (dev_type == (uint8_t)MODBUS_DEV_TYPE_MBSL32DI ||
                dev_type == (uint8_t)MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH)
            {
                continue;
            }
        }
        else
        {
            /* Hybrid full or control-only: Waveshare relay only needed if total_units > 4 */
            if (dev_type == (uint8_t)MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH && total_units <= 4U)
            {
                continue;
            }
        }

        err = modbus_registry_register(&self->modbus.registry_impl, &default_slaves[i]);
        if (err != 0)
        {
            LOG_ERR("registering device %u failed (%d)", default_slaves[i].device_id, err);
            return err;
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

    modbus_slave_descriptor_t ws_desc;
    if (modbus_registry_find_by_type(&self->modbus.registry_impl,
                                     MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
                                     &ws_desc) == 0)
    {
        uint8_t waveshare_bus = ws_desc.bus_id;
        if (waveshare_bus >= (uint8_t)MODBUS_BUS_MAX)
        {
            waveshare_bus = (uint8_t)MODBUS_BUS_RS485_1;
        }

        err = adapter_relay_modbus_init(&self->modbus.waveshare_relay,
                                        &self->modbus.ao[waveshare_bus],
                                        ws_desc.slave_addr,
                                        (int32_t)ws_desc.timeout_ms);
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
    }
    else
    {
        self->modbus.relay_ext_port.api = NULL;
        self->modbus.relay_ext_port.impl = NULL;
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

    /* Initialize RAM defaults for persistent configuration domains */
    self->device_cfg = *default_config_get_device_cfg();
    self->calibration_cfg = *default_config_get_calibration_cfg();
    self->hvac.config = *default_config_get_hvac_profile(HVAC_MODE_HYBRID_FULL);
    self->thermal_site_cfg = *default_config_get_thermal_site_cfg();
    self->thermal_baseline_cfg = *default_config_get_thermal_baseline_cfg();

    if (i_config_storage_is_bound(&self->storage.port))
    {
        /* 1. DEVICE */
        uint8_t dev_buf[sizeof(config_envelope_header_t) + sizeof(config_device_t)];
        size_t dev_size = 0U;
        config_device_t dev_cfg;
        memset(&dev_cfg, 0, sizeof(dev_cfg));
        size_t dev_unpacked = 0U;
        if (i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_DEVICE,
                                  dev_buf, sizeof(dev_buf), &dev_size) == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_DEVICE, dev_buf, dev_size,
                                   &dev_cfg, sizeof(dev_cfg), &dev_unpacked) == 0 &&
            dev_unpacked == sizeof(config_device_t))
        {
            self->device_cfg = dev_cfg;
            LOG_INF("Loaded DEVICE config from storage");
        }

        /* 2. CALIBRATION */
        uint8_t cal_buf[sizeof(config_envelope_header_t) + sizeof(config_calibration_t)];
        size_t cal_size = 0U;
        config_calibration_t cal_cfg;
        memset(&cal_cfg, 0, sizeof(cal_cfg));
        size_t cal_unpacked = 0U;
        if (i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_CALIBRATION,
                                  cal_buf, sizeof(cal_buf), &cal_size) == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_CALIBRATION, cal_buf, cal_size,
                                   &cal_cfg, sizeof(cal_cfg), &cal_unpacked) == 0 &&
            cal_unpacked == sizeof(config_calibration_t))
        {
            self->calibration_cfg = cal_cfg;
            LOG_INF("Loaded CALIBRATION config from storage");
        }

        /* 3. HVAC */
        uint8_t hvac_buf[sizeof(config_envelope_header_t) + sizeof(config_hvac_t)];
        size_t hvac_size = 0U;
        config_hvac_t hvac_cfg;
        memset(&hvac_cfg, 0, sizeof(hvac_cfg));
        size_t hvac_unpacked = 0U;
        if (i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_HVAC,
                                  hvac_buf, sizeof(hvac_buf), &hvac_size) == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_HVAC, hvac_buf, hvac_size,
                                   &hvac_cfg, sizeof(hvac_cfg), &hvac_unpacked) == 0 &&
            hvac_unpacked == sizeof(config_hvac_t) &&
            config_hvac_is_valid(&hvac_cfg))
        {
            self->hvac.config = hvac_cfg;
            LOG_INF("Loaded HVAC config from storage (mode=%u, units=%u)",
                    (unsigned int)self->hvac.config.operation_mode,
                    (unsigned int)self->hvac.config.total_units);
        }
        else
        {
            LOG_INF("No valid HVAC config in storage, applying defaults");
            size_t pack_len = 0U;
            if (config_envelope_pack(CONFIG_DOMAIN_HVAC, &self->hvac.config,
                                     sizeof(self->hvac.config), hvac_buf,
                                     sizeof(hvac_buf), &pack_len) == 0)
            {
                int save_rc = i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_HVAC,
                                                    hvac_buf, pack_len);
                if (save_rc != 0)
                {
                    LOG_ERR("Failed to persist default HVAC config: %d", save_rc);
                }
            }
        }

        /* 4. THERMAL_SITE */
        uint8_t site_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_site_t)];
        size_t site_size = 0U;
        config_thermal_site_t site_cfg;
        memset(&site_cfg, 0, sizeof(site_cfg));
        size_t site_unpacked = 0U;
        if (i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_THERMAL_SITE,
                                  site_buf, sizeof(site_buf), &site_size) == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_THERMAL_SITE, site_buf, site_size,
                                   &site_cfg, sizeof(site_cfg), &site_unpacked) == 0 &&
            site_unpacked == sizeof(config_thermal_site_t) &&
            config_thermal_site_is_valid(&site_cfg))
        {
            self->thermal_site_cfg = site_cfg;
            LOG_INF("Loaded THERMAL_SITE config from storage (pairs=%u, altitude=%d m)",
                    (unsigned int)self->thermal_site_cfg.configured_pairs_count,
                    (int)self->thermal_site_cfg.site_altitude_m);
        }
        else
        {
            LOG_INF("No valid THERMAL_SITE config in storage, applying defaults");
            size_t pack_len = 0U;
            if (config_envelope_pack(CONFIG_DOMAIN_THERMAL_SITE, &self->thermal_site_cfg,
                                     sizeof(self->thermal_site_cfg), site_buf,
                                     sizeof(site_buf), &pack_len) == 0)
            {
                int save_rc = i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_THERMAL_SITE,
                                                    site_buf, pack_len);
                if (save_rc != 0)
                {
                    LOG_ERR("Failed to persist default THERMAL_SITE config: %d", save_rc);
                }
            }
        }

        /* 5. THERMAL_BASELINE */
        uint8_t base_buf[sizeof(config_envelope_header_t) + sizeof(config_thermal_baseline_t)];
        size_t base_size = 0U;
        config_thermal_baseline_t base_cfg;
        memset(&base_cfg, 0, sizeof(base_cfg));
        size_t base_unpacked = 0U;
        if (i_config_storage_load(&self->storage.port, CONFIG_DOMAIN_THERMAL_BASELINE,
                                  base_buf, sizeof(base_buf), &base_size) == 0 &&
            config_envelope_unpack(CONFIG_DOMAIN_THERMAL_BASELINE, base_buf, base_size,
                                   &base_cfg, sizeof(base_cfg), &base_unpacked) == 0 &&
            base_unpacked == sizeof(config_thermal_baseline_t) &&
            config_thermal_baseline_is_valid(&base_cfg))
        {
            self->thermal_baseline_cfg = base_cfg;
            LOG_INF("Loaded THERMAL_BASELINE config from storage (valid=%u, hours=%u)",
                    (unsigned int)self->thermal_baseline_cfg.is_valid,
                    (unsigned int)self->thermal_baseline_cfg.training_hours_accumulated);
        }
        else
        {
            LOG_INF("No valid THERMAL_BASELINE config in storage, applying defaults");
            size_t pack_len = 0U;
            if (config_envelope_pack(CONFIG_DOMAIN_THERMAL_BASELINE, &self->thermal_baseline_cfg,
                                     sizeof(self->thermal_baseline_cfg), base_buf,
                                     sizeof(base_buf), &pack_len) == 0)
            {
                int save_rc = i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_THERMAL_BASELINE,
                                                    base_buf, pack_len);
                if (save_rc != 0)
                {
                    LOG_ERR("Failed to persist default THERMAL_BASELINE config: %d", save_rc);
                }
            }
        }
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

    if (!self->storage.initialized)
    {
        LOG_ERR("storage not initialized; boot sequence requires storage before network");
        return -EAGAIN;
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
                int save_rc = i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_NET_ETH,
                                                    eth_buf, pack_len);
                if (save_rc != 0)
                {
                    LOG_ERR("Failed to persist default net_eth configuration: %d", save_rc);
                }
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
                int save_rc = i_config_storage_save(&self->storage.port, CONFIG_DOMAIN_NET_WIFI,
                                                    wifi_buf, pack_len);
                if (save_rc != 0)
                {
                    LOG_ERR("Failed to persist default net_wifi configuration: %d", save_rc);
                }
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

    if (!self->storage.initialized)
    {
        LOG_ERR("storage not initialized; boot sequence requires storage before di");
        return -EAGAIN;
    }

    k_msgq_init(&self->di.queue, self->di.queue_buf,
                sizeof(ao_di_event_t), AO_DI_QUEUE_DEPTH);

    hvac_operation_mode_t op_mode = (hvac_operation_mode_t)self->hvac.config.operation_mode;
    size_t di_count = 0U;
    const di_channel_config_t *di_cfg = hardware_pin_map_get_di_profile(op_mode, &di_count);

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

    int err = di_aggregator_init(&self->di.aggregator, di_cfg,
                                 di_count,
                                 adapter_di_gpio_read, &self->di.di_adapter);
    if (err != 0)
    {
        LOG_ERR("di_aggregator_init failed (%d)", err);
        return err;
    }
#else
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

    if (!self->storage.initialized)
    {
        LOG_ERR("storage not initialized; boot sequence requires storage before hvac");
        return -EAGAIN;
    }

    /* 1. Ensure HVAC configuration in RAM is valid (loaded during storage init) */
    if (!config_hvac_is_valid(&self->hvac.config))
    {
        self->hvac.config = *default_config_get_hvac_profile(HVAC_MODE_HYBRID_FULL);
    }
    hvac_operation_mode_t op_mode = (hvac_operation_mode_t)self->hvac.config.operation_mode;

    /* 2. Bind remote relay port to waveshare relay if available */
    i_relay_t remote_relay = {0};
    if (i_relay_is_bound(&self->modbus.relay_ext_port))
    {
        remote_relay = self->modbus.relay_ext_port;
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

    /* 4. Build unit hardware routing for current operational mode */
    hvac_unit_hw_config_t hw_units[HVAC_MAX_UNITS_TOTAL];
    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++)
    {
        const hvac_unit_hw_config_t *u_cfg = hardware_pin_map_get_unit_for_mode(op_mode, u);
        if (u_cfg != NULL)
        {
            hw_units[u] = *u_cfg;
        }
        else
        {
            (void)memset(&hw_units[u], 0, sizeof(hw_units[u]));
        }
    }

    /* 5. Initialize actuator adapter */
    int err = adapter_hvac_actuator_init(&self->hvac.actuator_adapter,
                                         hw_units,
                                         HVAC_MAX_UNITS_TOTAL,
                                         i_relay_is_bound(&remote_relay) ? &remote_relay : NULL,
                                         i_relay_is_bound(&self->hvac.local_relay_port) ? &self->hvac.local_relay_port : NULL);
    if (err != 0)
    {
        LOG_ERR("adapter_hvac_actuator_init failed (%d)", err);
        return err;
    }
    (void)adapter_hvac_actuator_apply_mode(
        &self->hvac.actuator_adapter,
        op_mode);
    (void)adapter_hvac_actuator_set_bypass_alarm_contact_type(
        &self->hvac.actuator_adapter,
        (hvac_alarm_contact_type_t)self->hvac.config.bypass_alarm_contact_type);
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

static void on_modbus_cwt_tm16pt_reading(void *ctx, uint8_t device_id, const temperature_bank_16ch_t *bank)
{
    app_container_t *self = (app_container_t *)ctx;
    if (self == NULL || bank == NULL)
    {
        return;
    }

    /* Ingest into the sensor matrix raw bank so the domain
     * applies calibration, open-probe detection, semantic routing, and Zbus publishing. */
    if (self->sensor.initialized && i_sensor_matrix_is_bound(&self->sensor.port))
    {
        uint8_t concentrator_idx = (device_id == MODBUS_ADDR_CWT_TM16PT_2) ? 1U : 0U;
        if (i_sensor_matrix_submit_cwt_bank(&self->sensor.port, concentrator_idx, bank) == 0)
        {
            (void)ao_sensor_matrix_trigger(&self->sensor.ao);
        }
    }
}

static void on_modbus_jsy_mk339_reading(void *ctx, uint8_t device_id, const energy_reading_t *reading)
{
    ARG_UNUSED(device_id);
    app_container_t *self = (app_container_t *)ctx;
    if (self == NULL || reading == NULL)
    {
        return;
    }

    if (self->alarm.initialized)
    {
        adapter_alarm_detector_volt_process_reading(&self->alarm.det_volt, reading);
    }

    if (self->thermal_energy.initialized)
    {
        (void)ao_thermal_energy_feed_energy_reading(&self->thermal_energy.ao, reading);
    }
}

static void on_modbus_bus_fault(void *ctx, uint8_t bus_id, bool is_fault)
{
    ARG_UNUSED(bus_id);
    app_container_t *self = (app_container_t *)ctx;
    if (self == NULL)
    {
        return;
    }

    if (self->alarm.initialized)
    {
        (void)ao_alarm_manager_post_condition(&self->alarm.ao,
                                              ALARM_ID_INFRA_MODBUS_FAULT,
                                              is_fault, 0.0f);
    }
}

int composition_root_wire(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->storage.initialized)
    {
        int reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_DEVICE,
                                                   storage_provider_device, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for DEVICE: %d", reg_err);
        }

        reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_NET_ETH,
                                               storage_provider_eth, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for NET_ETH: %d", reg_err);
        }

        reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_NET_WIFI,
                                               storage_provider_wifi, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for NET_WIFI: %d", reg_err);
        }

        reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_CALIBRATION,
                                               storage_provider_calibration, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for CALIBRATION: %d", reg_err);
        }

        if (self->hvac.initialized)
        {
            reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_HVAC,
                                                   storage_provider_hvac, self);
            if (reg_err != 0)
            {
                LOG_WRN("failed to register storage provider for HVAC: %d", reg_err);
            }
        }

        reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_THERMAL_SITE,
                                               storage_provider_thermal_site, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for THERMAL_SITE: %d", reg_err);
        }

        reg_err = ao_storage_register_provider(&self->storage.ao, CONFIG_DOMAIN_THERMAL_BASELINE,
                                               storage_provider_thermal_baseline, self);
        if (reg_err != 0)
        {
            LOG_WRN("failed to register storage provider for THERMAL_BASELINE: %d", reg_err);
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
            ao_modbus_set_cwt_tm16pt_callback(&self->modbus.ao[bus], on_modbus_cwt_tm16pt_reading, self);
            ao_modbus_set_jsy_mk339_callback(&self->modbus.ao[bus], on_modbus_jsy_mk339_reading, self);
            ao_modbus_set_bus_fault_callback(&self->modbus.ao[bus], on_modbus_bus_fault, self);
        }
    }

    if (self->thermal_energy.initialized)
    {
        int err = composition_root_wire_thermal_energy(self);
        if (err != 0)
        {
            LOG_WRN("failed to wire thermal energy: %d", err);
        }
    }

    return 0;
}

/* =========================================================================
 * Full Lifecycle Management
 * ========================================================================= */

/**
 * @brief Initialize the Sensor Matrix Ingestion subsystem.
 *
 * Binds the ADC DMA adapter to the sensor matrix port, applies the default
 * pilot channel configuration table, and creates the acquisition Active Object.
 */
int composition_root_init_sensor_matrix(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->sensor.initialized)
    {
        return 0;
    }

    /* Obtain the ADC DMA engine device from devicetree when the board provides it. */
    int err;

#if defined(CONFIG_ZTEST)
    /*
     * Host test builds carry no ADC DMA engine node, so bind the in-memory fake.
     * The Active Object and the Modbus CWT ingestion path stay exercisable.
     */
    self->sensor.port = fake_sensor_matrix_get_port();
#else
#if DT_NODE_EXISTS(DT_NODELABEL(adc_dma_engine))
    const struct device *adc_dma_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(adc_dma_engine));
#else
    const struct device *adc_dma_dev = NULL;
#endif

    if (adc_dma_dev == NULL || !device_is_ready(adc_dma_dev))
    {
        LOG_WRN("ADC DMA engine unavailable; sensor matrix runs on external banks only");
        self->sensor.port.api = NULL;
        self->sensor.port.impl = NULL;
        self->sensor.initialized = true;
        return 0;
    }

    /* Initialise the Zephyr ADC DMA adapter. */
    err = adapter_sensor_zephyr_init(&self->sensor.adapter,
                                     adc_dma_dev,
                                     AO_SENSOR_MATRIX_PERIOD_MS);
    if (err != 0)
    {
        LOG_ERR("sensor adapter init failed (%d)", err);
        return err;
    }

    /* Bind the port handle. */
    err = adapter_sensor_zephyr_port(&self->sensor.adapter, &self->sensor.port);
    if (err != 0)
    {
        LOG_ERR("sensor matrix port binding failed (%d)", err);
        return err;
    }
#endif /* CONFIG_ZTEST */

    /* Apply the default pilot channel configuration table. */
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    err = sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);
    if (err != 0)
    {
        LOG_ERR("default pilot config failed (%d)", err);
        return err;
    }

    for (size_t i = 0U; i < SENSOR_PHYS_CH_MAX; i++)
    {
        if (configs[i].role == (uint8_t)SENSOR_ROLE_DISABLED)
        {
            continue;
        }
        err = i_sensor_matrix_set_channel_config(&self->sensor.port,
                                                 (physical_channel_id_t)i,
                                                 &configs[i]);
        if (err != 0)
        {
            LOG_WRN("channel %u config failed (%d)", (unsigned int)i, err);
        }
    }

    /* Initialise the command queue. */
    k_msgq_init(&self->sensor.queue,
                self->sensor.queue_buf,
                sizeof(ao_sensor_matrix_event_t),
                AO_SENSOR_MATRIX_QUEUE_DEPTH);

    /* Initialise the acquisition Active Object (thread spawned once, gated). */
    const ao_sensor_matrix_config_t ao_cfg = {
        .matrix = &self->sensor.port,
        .poll_period_ms = AO_SENSOR_MATRIX_PERIOD_MS,
        .stack = s_ao_sensor_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(s_ao_sensor_stack),
    };

    err = ao_sensor_matrix_init(&self->sensor.ao, &ao_cfg, &self->sensor.queue);
    if (err != 0)
    {
        LOG_ERR("sensor matrix AO init failed (%d)", err);
        return err;
    }

    self->sensor.initialized = true;
    LOG_INF("sensor matrix subsystem initialised");

    return 0;
}


int composition_root_init_alarm(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->alarm.initialized)
    {
        return 0;
    }

    /* 1. Initialize Active Object */
    int err = ao_alarm_manager_init(&self->alarm.ao,
                                    s_ao_alarm_stack,
                                    K_THREAD_STACK_SIZEOF(s_ao_alarm_stack),
                                    7);
    if (err != 0)
    {
        LOG_ERR("ao_alarm_manager_init failed (%d)", err);
        return err;
    }

    self->alarm.query_port = ao_alarm_manager_get_query_port(&self->alarm.ao);

    /* 2. Initialize Ingestion Detectors */
    adapter_alarm_detector_di_init(&self->alarm.det_di, &self->alarm.ao);

    /* Temperature: initialized and bound to persistent HVAC config loaded from EEPROM */
    adapter_alarm_detector_temp_init(&self->alarm.det_temp, &self->alarm.ao);
    if (self->hvac.initialized)
    {
        (void)adapter_alarm_detector_temp_apply_hvac_config(&self->alarm.det_temp, &self->hvac.config);
    }

    /* Voltage: nominal 220V, deviation 8%, hysteresis 2V */
    adapter_alarm_detector_volt_init(&self->alarm.det_volt, &self->alarm.ao,
                                      220.0f, 8.0f, 2.0f);

    /* HVAC: bypass & lockout detector */
    adapter_alarm_detector_hvac_init(&self->alarm.det_hvac, &self->alarm.ao);

    if (self->hvac.initialized && self->indicators.initialized)
    {
        (void)ao_system_indicator_set_buzzer_muted(&self->indicators.ao,
                                                   self->hvac.config.alarm_buzzer_muted != 0U);
    }

    self->alarm.initialized = true;
    LOG_INF("alarm management subsystem initialised");
    return 0;
}

/* =========================================================================
 * Zbus Observers & Listeners
 * ========================================================================= */

/*
 * Note on Invariant 6: Zephyr Zbus listener callbacks (ZBUS_LISTENER_DEFINE) have a fixed
 * C signature `void (*)(const struct zbus_channel *chan)` that passes no user_data context.
 * s_active_container holds strictly an instance reference to the active container (allocated
 * in main.c or test suites) to route Zbus notifications to the appropriate subsystems,
 * without maintaining any mutable operational state at file scope.
 */
static app_container_t *s_active_container = NULL;

static void on_hvac_sensor_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_sensor_snapshot || s_active_container == NULL)
    {
        return;
    }

    const sensor_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (s_active_container->hvac.initialized)
    {
        (void)ao_hvac_feed_sensor_snapshot(&s_active_container->hvac.ao,
                                           msg->temperatures,
                                           msg->valid_mask,
                                           64U);
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_sensor_feed, on_hvac_sensor_feed_cb);

static void on_hvac_di_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_digital_input_snapshot || s_active_container == NULL)
    {
        return;
    }

    const di_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (!s_active_container->hvac.initialized)
    {
        return;
    }

    /* 1. Fire alarm safety interlock */
    bool fire_active = false;
    for (uint8_t i = 0U; i < msg->channel_count; i++)
    {
        if (msg->roles[i] == DI_ROLE_SMOKE_FIRE)
        {
            fire_active = msg->is_active[i];
            break;
        }
    }
    (void)ao_hvac_set_fire(&s_active_container->hvac.ao, fire_active);

    /* 2. Physical bypass switch interlock */
    bool bypass_sw_active = false;
    for (uint8_t i = 0U; i < msg->channel_count; i++)
    {
        if (msg->roles[i] == DI_ROLE_BYPASS_SWITCH)
        {
            bypass_sw_active = msg->is_active[i];
            break;
        }
    }
    for (uint8_t g = 0U; g < (uint8_t)HVAC_MAX_GROUPS; g++)
    {
        (void)ao_hvac_set_bypass_cause(&s_active_container->hvac.ao, g,
                                       BYPASS_CAUSE_PHYSICAL_SWITCH, bypass_sw_active);
    }

    /* 3. HVAC Unit Mechanical Alarms (Units 0..7) */
    for (uint8_t u = 0U; u < (uint8_t)HVAC_MAX_UNITS_TOTAL; u++)
    {
        bool hp = false;
        bool lp = false;
        bool flow = false;
        bool volt = false;

        di_role_t base_role = (di_role_t)(DI_ROLE_UNIT_0_ALARM_HP + (u * 4U));

        for (uint8_t i = 0U; i < msg->channel_count; i++)
        {
            if (msg->roles[i] == base_role)
            {
                hp = msg->is_active[i];
            }
            else if (msg->roles[i] == (di_role_t)(base_role + 1U))
            {
                lp = msg->is_active[i];
            }
            else if (msg->roles[i] == (di_role_t)(base_role + 2U))
            {
                flow = msg->is_active[i];
            }
            else if (msg->roles[i] == (di_role_t)(base_role + 3U))
            {
                volt = msg->is_active[i];
            }
        }

        (void)ao_hvac_set_unit_alarms(&s_active_container->hvac.ao, u, hp, lp, flow, volt);
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_di_feed, on_hvac_di_feed_cb);

static void on_hvac_event_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_hvac_event)
    {
        return;
    }

    const hvac_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL)
    {
        return;
    }

    LOG_INF("HVAC Event: grp=%u unit=%u type=%d ts=%u",
            ev->group_id, ev->unit_id, (int)ev->type, ev->timestamp_ms);
}

ZBUS_LISTENER_DEFINE(lis_hvac_event_logger, on_hvac_event_cb);

static void on_alarm_di_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_digital_input_state || s_active_container == NULL)
    {
        return;
    }

    const di_transition_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL)
    {
        return;
    }

    if (s_active_container->alarm.initialized)
    {
        adapter_alarm_detector_di_process_transition(&s_active_container->alarm.det_di, ev);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_di_feed, on_alarm_di_feed_cb);

static void on_alarm_sensor_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_sensor_snapshot || s_active_container == NULL)
    {
        return;
    }

    const sensor_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (s_active_container->alarm.initialized)
    {
        adapter_alarm_detector_temp_process_snapshot(&s_active_container->alarm.det_temp, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_sensor_feed, on_alarm_sensor_feed_cb);

static void on_alarm_hvac_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_hvac_status || s_active_container == NULL)
    {
        return;
    }

    const hvac_status_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (s_active_container->alarm.initialized)
    {
        adapter_alarm_detector_hvac_process_status(&s_active_container->alarm.det_hvac, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_hvac_feed, on_alarm_hvac_feed_cb);

static void on_indicator_alarm_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_system_alarm || s_active_container == NULL)
    {
        return;
    }

    const alarm_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (s_active_container->indicators.initialized)
    {
        (void)ao_system_indicator_post_alarm(&s_active_container->indicators.ao, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_indicator_alarm_feed, on_indicator_alarm_feed_cb);

static void on_alarm_transition_logger_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_alarm_transition)
    {
        return;
    }

    const alarm_transition_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL)
    {
        return;
    }

    const char *tr_str = "UNKNOWN";
    switch (ev->event_type)
    {
    case ALARM_EVENT_ACTIVATED:      tr_str = "ACTIVATED"; break;
    case ALARM_EVENT_CLEARED:        tr_str = "CLEARED"; break;
    case ALARM_EVENT_ACKNOWLEDGED:   tr_str = "ACKNOWLEDGED"; break;
    case ALARM_EVENT_LATCHED:        tr_str = "LATCHED"; break;
    case ALARM_EVENT_SOURCE_CHANGED: tr_str = "SRC_CHANGED"; break;
    default: break;
    }

    LOG_INF("ALARM [0x%04X] %s: state=%d sev=%d val=%.2f mask=0x%08X ts=%llu ms",
            ev->alarm_id, tr_str, (int)ev->new_state, (int)ev->severity,
            (double)ev->trigger_value, (unsigned int)ev->contributing_sources_mask,
            (unsigned long long)ev->timestamp_ms);
}

ZBUS_LISTENER_DEFINE(lis_alarm_logger, on_alarm_transition_logger_cb);

static void on_alarm_net_eth_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_network_eth_state || s_active_container == NULL)
    {
        return;
    }

    const net_state_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL)
    {
        return;
    }

    if (s_active_container->alarm.initialized)
    {
        bool is_down = (msg->status.state != NET_IF_STATE_CONNECTED);
        (void)ao_alarm_manager_post_condition(&s_active_container->alarm.ao,
                                              ALARM_ID_INFRA_ETHERNET_DOWN,
                                              is_down, 0.0f);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_net_eth_feed, on_alarm_net_eth_feed_cb);

static void on_thermal_baseline_persisted(void *user_data, const config_thermal_baseline_t *baseline)
{
    app_container_t *app = (app_container_t *)user_data;
    if (app == NULL || baseline == NULL)
    {
        return;
    }

    if (app->storage.initialized)
    {
        (void)ao_storage_mark_dirty(&app->storage.ao, CONFIG_DOMAIN_THERMAL_BASELINE, 0U);
    }
}

int composition_root_init_thermal_energy(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->thermal_energy.initialized)
    {
        return 0;
    }

    int err = ao_thermal_energy_init(&self->thermal_energy.ao,
                                     &self->thermal_site_cfg,
                                     &self->thermal_baseline_cfg,
                                     s_ao_thermal_energy_stack,
                                     K_THREAD_STACK_SIZEOF(s_ao_thermal_energy_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    if (err != 0)
    {
        LOG_ERR("Failed to initialize ao_thermal_energy: %d", err);
        return err;
    }

    self->thermal_energy.port = ao_thermal_energy_get_port(&self->thermal_energy.ao);
    self->thermal_energy.initialized = true;
    return 0;
}

int composition_root_wire_thermal_energy(app_container_t *self)
{
    if (self == NULL || !self->thermal_energy.initialized)
    {
        return -EINVAL;
    }

    ao_thermal_energy_set_baseline_persist_cb(&self->thermal_energy.ao,
                                              on_thermal_baseline_persisted,
                                              self);
    return 0;
}

i_thermal_energy_t composition_root_get_thermal_energy_port(app_container_t *self)
{
    if (self == NULL || !self->thermal_energy.initialized)
    {
        i_thermal_energy_t unbound = {0};
        return unbound;
    }

    return self->thermal_energy.port;
}



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
    s_active_container = self;

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

    err = composition_root_init_alarm(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_sensor_matrix(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_thermal_energy(self);
    if (err != 0)
    {
        return err;
    }

    err = composition_root_init_network(self);
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

    if (self->alarm.initialized)
    {
        int err = ao_alarm_manager_start(&self->alarm.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("alarm active object start failed (%d)", err);
            return err;
        }
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

    if (self->sensor.initialized)
    {
        int err = ao_sensor_matrix_start(&self->sensor.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("sensor matrix active object start failed (%d)", err);
            return err;
        }
    }

    if (self->thermal_energy.initialized)
    {
        int err = ao_thermal_energy_start(&self->thermal_energy.ao);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("thermal energy active object start failed (%d)", err);
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

    int rc = 0;

    /* 1. Stop network coordinator (wait up to 2000 ms) */
    if (self->network.initialized)
    {
        rc = ao_network_stop(&self->network.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_network_stop returned %d", rc);
        }
        rc = ao_network_wait_stopped(&self->network.ao, K_MSEC(2000));
        if (rc != 0)
        {
            LOG_WRN("ao_network_wait_stopped returned %d", rc);
        }
    }

    /* 1b. Stop thermal energy monitoring (wait up to 1000 ms) */
    if (self->thermal_energy.initialized)
    {
        rc = ao_thermal_energy_stop(&self->thermal_energy.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_thermal_energy_stop returned %d", rc);
        }
        rc = ao_thermal_energy_wait_stopped(&self->thermal_energy.ao, K_MSEC(1000));
        if (rc != 0)
        {
            LOG_WRN("ao_thermal_energy_wait_stopped returned %d", rc);
        }
    }

    /* 2. Stop sensor matrix acquisition (wait up to 1000 ms) */
    if (self->sensor.initialized)
    {
        rc = ao_sensor_matrix_stop(&self->sensor.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_sensor_matrix_stop returned %d", rc);
        }
        rc = ao_sensor_matrix_wait_stopped(&self->sensor.ao, K_MSEC(1000));
        if (rc != 0)
        {
            LOG_WRN("ao_sensor_matrix_wait_stopped returned %d", rc);
        }
    }

    /* 3. Stop HVAC coordinator (wait up to 1000 ms) */
    if (self->hvac.initialized)
    {
        rc = ao_hvac_stop(&self->hvac.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_hvac_stop returned %d", rc);
        }
        rc = ao_hvac_wait_stopped(&self->hvac.ao, K_MSEC(1000));
        if (rc != 0)
        {
            LOG_WRN("ao_hvac_wait_stopped returned %d", rc);
        }
    }

    /* 4. Stop digital inputs (wait up to 200 ms) */
    if (self->di.initialized)
    {
        rc = ao_digital_input_stop(&self->di.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_digital_input_stop returned %d", rc);
        }
        rc = ao_digital_input_wait_stopped(&self->di.ao, K_MSEC(200));
        if (rc != 0)
        {
            LOG_WRN("ao_digital_input_wait_stopped returned %d", rc);
        }
    }

    /* 5. Stop Modbus polling threads (wait up to 1000 ms) */
    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            rc = ao_modbus_stop(&self->modbus.ao[bus]);
            if (rc != 0 && rc != -EALREADY)
            {
                LOG_WRN("ao_modbus_stop bus %u returned %d", bus, rc);
            }
            rc = ao_modbus_wait_stopped(&self->modbus.ao[bus], K_MSEC(1000));
            if (rc != 0)
            {
                LOG_WRN("ao_modbus_wait_stopped bus %u returned %d", bus, rc);
            }
        }
    }

    /* 5b. Stop alarm manager (wait up to 500 ms) */
    if (self->alarm.initialized)
    {
        rc = ao_alarm_manager_stop(&self->alarm.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_alarm_manager_stop returned %d", rc);
        }
        rc = ao_alarm_manager_wait_stopped(&self->alarm.ao, K_MSEC(500));
        if (rc != 0)
        {
            LOG_WRN("ao_alarm_manager_wait_stopped returned %d", rc);
        }
    }

    /* 6. Stop system indicators (wait up to 200 ms) */
    if (self->indicators.initialized)
    {
        rc = ao_system_indicator_stop(&self->indicators.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_system_indicator_stop returned %d", rc);
        }
        rc = ao_system_indicator_wait_stopped(&self->indicators.ao, K_MSEC(200));
        if (rc != 0)
        {
            LOG_WRN("ao_system_indicator_wait_stopped returned %d", rc);
        }
    }

    /* 7. Stop storage coordinator (wait up to 2000 ms - flushes pending changes) */
    if (self->storage.initialized)
    {
        rc = ao_storage_stop(&self->storage.ao);
        if (rc != 0 && rc != -EALREADY)
        {
            LOG_WRN("ao_storage_stop returned %d", rc);
        }
        rc = ao_storage_wait_stopped(&self->storage.ao, K_MSEC(2000));
        if (rc != 0)
        {
            LOG_WRN("ao_storage_wait_stopped returned %d", rc);
        }
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
    int rc = composition_root_stop(self);
    if (rc != 0)
    {
        LOG_WRN("composition_root_stop returned %d during deinit", rc);
    }

    /* Deinitialize subsystems in reverse order of initialization */
    if (self->network.initialized)
    {
        rc = ao_network_deinit(&self->network.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_network_deinit returned %d", rc);
        }
        self->network.initialized = false;
    }

    if (self->hvac.initialized)
    {
        rc = ao_hvac_deinit(&self->hvac.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_hvac_deinit returned %d", rc);
        }
        self->hvac.initialized = false;
    }

    if (self->thermal_energy.initialized)
    {
        rc = ao_thermal_energy_deinit(&self->thermal_energy.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_thermal_energy_deinit returned %d", rc);
        }
        self->thermal_energy.initialized = false;
    }

    if (self->sensor.initialized)
    {
        if (self->sensor.ao.is_initialized)
        {
            rc = ao_sensor_matrix_deinit(&self->sensor.ao);
            if (rc != 0)
            {
                LOG_WRN("ao_sensor_matrix_deinit returned %d", rc);
            }
        }
        self->sensor.initialized = false;
    }

    if (self->di.initialized)
    {
        rc = ao_digital_input_deinit(&self->di.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_digital_input_deinit returned %d", rc);
        }
        self->di.initialized = false;
    }

    if (self->alarm.initialized)
    {
        rc = ao_alarm_manager_deinit(&self->alarm.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_alarm_manager_deinit returned %d", rc);
        }
        self->alarm.initialized = false;
    }

    if (self->indicators.initialized)
    {
        rc = ao_system_indicator_deinit(&self->indicators.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_system_indicator_deinit returned %d", rc);
        }
        self->indicators.initialized = false;
    }

    if (self->modbus.initialized)
    {
        for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
        {
            rc = ao_modbus_deinit(&self->modbus.ao[bus]);
            if (rc != 0)
            {
                LOG_WRN("ao_modbus_deinit bus %u returned %d", bus, rc);
            }
        }
        self->modbus.initialized = false;
    }

    if (self->storage.initialized)
    {
        rc = ao_storage_deinit(&self->storage.ao);
        if (rc != 0)
        {
            LOG_WRN("ao_storage_deinit returned %d", rc);
        }
        self->storage.initialized = false;
    }

    s_active_container = NULL;
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

const i_sensor_matrix_t *app_container_get_sensor_matrix(const app_container_t *self)
{
    if (self == NULL || !self->sensor.initialized)
    {
        return NULL;
    }
    return &self->sensor.port;
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

    if (self->alarm.initialized)
    {
        (void)adapter_alarm_detector_temp_set_group_from_hvac(&self->alarm.det_temp, group_id, cfg);
    }

    if (self->storage.initialized)
    {
        int rc = ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_HVAC, (uint16_t)group_id);
        if (rc != 0)
        {
            LOG_WRN("failed to mark storage dirty for HVAC group %u: %d", group_id, rc);
        }
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
        int act_rc = adapter_hvac_actuator_apply_mode(&self->hvac.actuator_adapter,
                                                      (hvac_operation_mode_t)cfg->operation_mode);
        if (act_rc != 0)
        {
            LOG_WRN("failed to apply mode %u to actuator: %d", (unsigned int)cfg->operation_mode, act_rc);
        }

        int err = ao_hvac_apply_config(&self->hvac.ao, cfg);
        if (err != 0)
        {
            return err;
        }
    }

    self->hvac.config = *cfg;

    if (self->alarm.initialized)
    {
        (void)adapter_alarm_detector_temp_apply_hvac_config(&self->alarm.det_temp, cfg);
    }

    if (self->storage.initialized)
    {
        int rc = ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_HVAC, 0U);
        if (rc != 0)
        {
            LOG_WRN("failed to mark storage dirty for HVAC: %d", rc);
        }
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



const i_alarm_query_t *app_container_get_alarm_query(const app_container_t *self)
{
    if (self == NULL || !self->alarm.initialized)
    {
        return NULL;
    }
    return &self->alarm.query_port;
}

ao_alarm_manager_t *app_container_get_alarm_manager_ao(app_container_t *self)
{
    if (self == NULL || !self->alarm.initialized)
    {
        return NULL;
    }
    return (ao_alarm_manager_t *)&self->alarm.ao;
}

int app_container_set_alarm_buzzer_mute(app_container_t *self, bool mute)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    if (self->hvac.initialized)
    {
        self->hvac.config.alarm_buzzer_muted = mute ? 1U : 0U;
    }

    if (self->indicators.initialized)
    {
        (void)ao_system_indicator_set_buzzer_muted(&self->indicators.ao, mute);
    }

    if (self->storage.initialized)
    {
        (void)ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_HVAC, 0U);
    }

    return 0;
}

bool app_container_is_alarm_buzzer_muted(const app_container_t *self)
{
    if (self == NULL)
    {
        return false;
    }

    if (self->indicators.initialized)
    {
        return ao_system_indicator_is_buzzer_muted(&self->indicators.ao);
    }

    if (self->hvac.initialized)
    {
        return (self->hvac.config.alarm_buzzer_muted != 0U);
    }

    return false;
}

int app_container_set_thermal_site_config(app_container_t *self, const config_thermal_site_t *cfg)
{
    if (self == NULL || cfg == NULL)
    {
        return -EINVAL;
    }

    if (!config_thermal_site_is_valid(cfg))
    {
        return -EINVAL;
    }

    self->thermal_site_cfg = *cfg;

    if (self->storage.initialized)
    {
        int rc = ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_THERMAL_SITE, 0U);
        if (rc != 0)
        {
            LOG_WRN("failed to mark storage dirty for THERMAL_SITE: %d", rc);
        }
    }

    return 0;
}

int app_container_set_thermal_baseline_config(app_container_t *self, const config_thermal_baseline_t *cfg)
{
    if (self == NULL || cfg == NULL)
    {
        return -EINVAL;
    }

    if (!config_thermal_baseline_is_valid(cfg))
    {
        return -EINVAL;
    }

    self->thermal_baseline_cfg = *cfg;

    if (self->storage.initialized)
    {
        int rc = ao_storage_mark_dirty(&self->storage.ao, CONFIG_DOMAIN_THERMAL_BASELINE, 0U);
        if (rc != 0)
        {
            LOG_WRN("failed to mark storage dirty for THERMAL_BASELINE: %d", rc);
        }
    }

    return 0;
}
