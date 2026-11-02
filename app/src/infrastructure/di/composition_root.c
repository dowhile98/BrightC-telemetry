/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "composition_root.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(composition_root, LOG_LEVEL_INF);

/**
 * Line configuration applied to a bus when its transport cannot report one.
 *
 * Also the boot configuration in host builds, where no transport is compiled
 * in and the tests bind a fake client port instead.
 */
static const modbus_bus_cfg_t k_fallback_bus_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
    [MODBUS_BUS_RS485_2] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
};

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

/**
 * Slave table applied at boot.
 *
 * A compile-time table, not mutable state: it will be replaced by the
 * persisted configuration once the settings backend lands.
 */
#ifdef CONFIG_ZTEST
static const modbus_slave_descriptor_t k_default_slaves[] = {
    {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = 2U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 2U,
        .dev_type = MODBUS_DEV_TYPE_JSY_MK339,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = 3U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 3U,
        .dev_type = MODBUS_DEV_TYPE_MBSL32DI,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = 4U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 4U,
        .dev_type = MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
};
#else
static const modbus_slave_descriptor_t k_default_slaves[] = {
    {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
   {
        .device_id = 2U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 2U,
        .dev_type = MODBUS_DEV_TYPE_JSY_MK339,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = 3U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 3U,
        .dev_type = MODBUS_DEV_TYPE_MBSL32DI,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
};
#endif

/**
 * Bring up the transport of every bus and bind its client port.
 *
 * A bus that fails to come up is left unbound and the others are still
 * started: the whole point of one polling thread per bus is that a dead wire
 * must not take the healthy one down with it. Only a controller with no
 * usable bus at all is a fatal error, because then nothing can ever be polled.
 */
static int init_transport(app_container_t *self)
{
    (void)memcpy(self->bus_cfg, k_fallback_bus_cfg, sizeof(self->bus_cfg));

#ifdef CONFIG_NANOMODBUS
    size_t bound_buses = 0U;

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        int err = adapter_modbus_nmbs_init(&self->nmbs[bus], &k_bus_transport_cfg[bus]);

        if (err == 0)
        {
            err = adapter_modbus_nmbs_port(&self->nmbs[bus], &self->client[bus]);
        }

        if (err == 0)
        {
            err = i_modbus_client_get_bus_cfg(&self->client[bus], &self->bus_cfg[bus]);
        }

        if (err != 0)
        {
            LOG_ERR("bus %u: transport unavailable (%d), continuing without it", bus, err);
            self->client[bus].api = NULL;
            self->client[bus].impl = NULL;
            continue;
        }

        bound_buses++;
    }

    if (bound_buses == 0U)
    {
        LOG_ERR("no Modbus transport could be brought up");
        return -ENODEV;
    }

    self->init_mask |= APP_CONTAINER_INIT_TRANSPORT;
#endif

    return 0;
}

static int init_registry(app_container_t *self)
{
    int err = modbus_registry_init(&self->registry_impl, self->bus_cfg);

    if (err != 0)
    {
        LOG_ERR("registry init failed (%d)", err);
        return err;
    }

    err = modbus_registry_port(&self->registry_impl, &self->registry);
    if (err != 0)
    {
        LOG_ERR("registry port binding failed (%d)", err);
        return err;
    }

    for (size_t i = 0U; i < ARRAY_SIZE(k_default_slaves); i++)
    {
        err = modbus_registry_register(&self->registry_impl, &k_default_slaves[i]);
        if (err != 0)
        {
            LOG_ERR("registering device %u failed (%d)", k_default_slaves[i].device_id, err);
            return err;
        }
    }

    self->init_mask |= APP_CONTAINER_INIT_REGISTRY;

    return 0;
}

static K_THREAD_STACK_ARRAY_DEFINE(g_ao_stacks, MODBUS_BUS_MAX, AO_MODBUS_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_net_stack, AO_NETWORK_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_ao_store_stack, AO_STORAGE_STACK_SIZE);

static const net_eth_config_t k_default_eth_cfg = {
    .ip_mode = NET_IP_MODE_DHCP,
    .auto_dhcpv4 = 1U,
    .ip_addr = 0xC0A80164,     /* 192.168.1.100 */
    .subnet_mask = 0xFFFFFF00, /* 255.255.255.0 */
    .gateway = 0xC0A80101,     /* 192.168.1.1 */
};

static const net_wifi_config_t k_default_wifi_cfg = {
    .mode = NET_WIFI_MODE_AP,
    .ap = {
        .ssid = "BrightC-AP",
        .psk = "12345678",
        .security = NET_WIFI_SEC_WPA2_PSK,
        .channel = 9U,
        .max_clients = 5U,
        .ap_ip_addr = 0xC0A80401,     /* 192.168.4.1 */
        .ap_subnet_mask = 0xFFFFFF00, /* 255.255.255.0 */
    },
};

static int storage_provider_eth(void *ctx, config_domain_t domain,
                                void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_NET_ETH || out_buf == NULL) {
        return -EINVAL;
    }
    if (i_network_is_bound(&app->net_eth)) {
        return i_network_get_config(&app->net_eth, NETWORK_INTERFACE_ETHERNET,
                                    out_buf, max_size, out_size);
    }
    if (max_size < sizeof(net_eth_config_t)) {
        return -ENOMEM;
    }
    (void)memcpy(out_buf, &app->ao_net.eth_cfg, sizeof(net_eth_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_eth_config_t);
    }
    return 0;
}

static int storage_provider_wifi(void *ctx, config_domain_t domain,
                                 void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_NET_WIFI || out_buf == NULL) {
        return -EINVAL;
    }
    if (i_network_is_bound(&app->net_wifi)) {
        return i_network_get_config(&app->net_wifi, NETWORK_INTERFACE_WIFI,
                                    out_buf, max_size, out_size);
    }
    if (max_size < sizeof(net_wifi_config_t)) {
        return -ENOMEM;
    }
    (void)memcpy(out_buf, &app->ao_net.wifi_cfg, sizeof(net_wifi_config_t));
    if (out_size != NULL) {
        *out_size = sizeof(net_wifi_config_t);
    }
    return 0;
}

static int storage_provider_modbus(void *ctx, config_domain_t domain,
                                   void *out_buf, size_t max_size, size_t *out_size)
{
    app_container_t *app = (app_container_t *)ctx;
    if (app == NULL || domain != CONFIG_DOMAIN_MODBUS || out_buf == NULL) {
        return -EINVAL;
    }
    return modbus_registry_export(&app->registry_impl, out_buf, max_size, out_size);
}

static int init_active_objects(app_container_t *self)
{
    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        k_msgq_init(&self->ao_queue[bus], self->ao_queue_buf[bus], sizeof(modbus_event_t),
                    AO_MODBUS_QUEUE_DEPTH);

        int err = ao_modbus_init(&self->ao[bus], bus, &self->registry, &self->client[bus],
                                 &self->ao_queue[bus], g_ao_stacks[bus], AO_MODBUS_STACK_SIZE);

        if (err != 0)
        {
            LOG_ERR("bus %u: active object init failed (%d)", bus, err);
            return err;
        }
    }

    self->init_mask |= APP_CONTAINER_INIT_AO;

    return 0;
}

#if defined(CONFIG_ZTEST)
#include "fake_network.h"
#include "fake_config_storage.h"
#endif

static int init_storage(app_container_t *self)
{
#if defined(CONFIG_ZTEST)
    if (!i_config_storage_is_bound(&self->storage)) {
        self->storage = fake_config_storage_get_port();
    }
#else
    const struct device *eeprom_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
    eeprom_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom0));
#endif
    if (eeprom_dev != NULL) {
        int store_err = adapter_storage_microconf_init(&self->storage_adapter, eeprom_dev);
        if (store_err == 0) {
            self->storage = adapter_storage_microconf_port(&self->storage_adapter);
        } else {
            LOG_WRN("adapter_storage_microconf_init failed (%d)", store_err);
        }
    } else {
        LOG_WRN("eeprom0 devicetree node not found");
    }
#endif

    if (!i_config_storage_is_bound(&self->storage)) {
        LOG_WRN("Persistent storage is not bound; storage subsystem operating in degraded mode");
    }

    k_msgq_init(&self->ao_store_queue, self->ao_store_queue_buf,
                sizeof(ao_storage_event_t), AO_STORAGE_QUEUE_DEPTH);

    int err = ao_storage_init(&self->ao_store, &self->storage,
                              &self->ao_store_queue, s_ao_store_stack,
                              K_THREAD_STACK_SIZEOF(s_ao_store_stack));
    if (err != 0) {
        LOG_ERR("storage active object init failed (%d)", err);
        return err;
    }

    (void)ao_storage_register_provider(&self->ao_store, CONFIG_DOMAIN_NET_ETH,
                                       storage_provider_eth, self);
    (void)ao_storage_register_provider(&self->ao_store, CONFIG_DOMAIN_NET_WIFI,
                                       storage_provider_wifi, self);
    (void)ao_storage_register_provider(&self->ao_store, CONFIG_DOMAIN_MODBUS,
                                       storage_provider_modbus, self);

    self->init_mask |= APP_CONTAINER_INIT_STORAGE;
    return 0;
}

static int init_network(app_container_t *self)
{
    k_msgq_init(&self->ao_net_queue, self->ao_net_queue_buf, sizeof(ao_network_event_t),
                AO_NETWORK_QUEUE_DEPTH);

#if defined(CONFIG_ZTEST)
    if (!i_network_is_bound(&self->net_eth)) {
        self->net_eth = fake_network_get_eth_port();
    }
    if (!i_network_is_bound(&self->net_wifi)) {
        self->net_wifi = fake_network_get_wifi_port();
    }
#else
    const struct device *eth_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(mac))
    eth_dev = DEVICE_DT_GET(DT_NODELABEL(mac));
#endif
    int eth_err = adapter_eth_stm32_init(&self->eth_adapter, eth_dev);
    if (eth_err == 0) {
        self->net_eth = adapter_eth_stm32_port(&self->eth_adapter);
    } else {
        LOG_WRN("adapter_eth_stm32_init failed (%d)", eth_err);
    }

    const struct device *wifi_dev = NULL;
#if DT_NODE_EXISTS(DT_NODELABEL(esp_hosted_mcu_wifi))
    wifi_dev = DEVICE_DT_GET(DT_NODELABEL(esp_hosted_mcu_wifi));
#endif
    int wifi_err = adapter_wifi_esp_hosted_init(&self->wifi_adapter, wifi_dev);
    if (wifi_err == 0) {
        self->net_wifi = adapter_wifi_esp_hosted_port(&self->wifi_adapter);
    } else {
        LOG_WRN("adapter_wifi_esp_hosted_init failed (%d)", wifi_err);
    }
#endif

    /* Synchronous boot read from storage for network configurations */
    net_eth_config_t loaded_eth_cfg;
    const net_eth_config_t *eth_cfg_ptr = &k_default_eth_cfg;
    size_t loaded_eth_size = 0;
    int eth_load_res = -1;

    if (i_config_storage_is_bound(&self->storage)) {
        eth_load_res = i_config_storage_load(&self->storage, CONFIG_DOMAIN_NET_ETH,
                                             &loaded_eth_cfg, sizeof(loaded_eth_cfg),
                                             &loaded_eth_size);
        if (eth_load_res == 0 && loaded_eth_size == sizeof(net_eth_config_t) &&
            net_eth_config_is_valid(&loaded_eth_cfg)) {
            LOG_INF("Loaded Ethernet config from storage");
            eth_cfg_ptr = &loaded_eth_cfg;
        } else {
            LOG_INF("No valid Ethernet config in storage (%d), applying defaults", eth_load_res);
            (void)i_config_storage_save(&self->storage, CONFIG_DOMAIN_NET_ETH,
                                        &k_default_eth_cfg, sizeof(k_default_eth_cfg));
        }
    }

    net_wifi_config_t loaded_wifi_cfg;
    const net_wifi_config_t *wifi_cfg_ptr = &k_default_wifi_cfg;
    size_t loaded_wifi_size = 0;
    int wifi_load_res = -1;

    if (i_config_storage_is_bound(&self->storage)) {
        wifi_load_res = i_config_storage_load(&self->storage, CONFIG_DOMAIN_NET_WIFI,
                                              &loaded_wifi_cfg, sizeof(loaded_wifi_cfg),
                                              &loaded_wifi_size);
        if (wifi_load_res == 0 && loaded_wifi_size == sizeof(net_wifi_config_t) &&
            net_wifi_config_is_valid(&loaded_wifi_cfg)) {
            LOG_INF("Loaded Wi-Fi config from storage");
            wifi_cfg_ptr = &loaded_wifi_cfg;
        } else {
            LOG_INF("No valid Wi-Fi config in storage (%d), applying defaults", wifi_load_res);
            (void)i_config_storage_save(&self->storage, CONFIG_DOMAIN_NET_WIFI,
                                        &k_default_wifi_cfg, sizeof(k_default_wifi_cfg));
        }
    }

    int err = ao_network_init(&self->ao_net, &self->net_eth, &self->net_wifi,
                              eth_cfg_ptr, wifi_cfg_ptr,
                              &self->ao_net_queue, s_ao_net_stack,
                              K_THREAD_STACK_SIZEOF(s_ao_net_stack));
    if (err != 0)
    {
        LOG_ERR("network active object init failed (%d)", err);
        return err;
    }

    self->init_mask |= APP_CONTAINER_INIT_NETWORK;
    return 0;
}

int composition_root_init(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    /* Idempotent: only a fully wired container is left alone. Requiring all
     * bits also keeps a container whose transport/registry step failed from
     * being mistaken for a finished one. */
    const uint8_t done = APP_CONTAINER_INIT_REGISTRY | APP_CONTAINER_INIT_AO |
                         APP_CONTAINER_INIT_STORAGE | APP_CONTAINER_INIT_NETWORK;

    if ((self->init_mask & done) == done)
    {
        return 0;
    }

    (void)memset(self, 0, sizeof(*self));

    int err = init_transport(self);

    if (err != 0)
    {
        return err;
    }

    err = init_registry(self);
    if (err != 0)
    {
        return err;
    }

    err = init_active_objects(self);
    if (err != 0)
    {
        return err;
    }

    err = adapter_relay_modbus_init(&self->waveshare_relay, &self->ao[MODBUS_BUS_RS485_1], 4U,
                                    300U);
    if (err != 0)
    {
        LOG_ERR("relay adapter init failed (%d)", err);
        return err;
    }

    err = adapter_relay_modbus_port(&self->waveshare_relay, &self->relay_ext);
    if (err != 0)
    {
        LOG_ERR("relay port binding failed (%d)", err);
        return err;
    }

    err = init_storage(self);
    if (err != 0)
    {
        return err;
    }

    err = init_network(self);
    if (err != 0)
    {
        return err;
    }

    return 0;
}

int composition_root_start(app_container_t *self)
{
    if (self == NULL || (self->init_mask & APP_CONTAINER_INIT_AO) == 0U)
    {
        return -EINVAL;
    }

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        int err = ao_modbus_start(&self->ao[bus]);

        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("bus %u: active object start failed (%d)", bus, err);
            return err;
        }
    }

    if ((self->init_mask & APP_CONTAINER_INIT_STORAGE) != 0U)
    {
        int err = ao_storage_start(&self->ao_store);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("storage active object start failed (%d)", err);
            return err;
        }
    }

    if ((self->init_mask & APP_CONTAINER_INIT_NETWORK) != 0U)
    {
        int err = ao_network_start(&self->ao_net);
        if (err != 0 && err != -EALREADY)
        {
            LOG_ERR("network active object start failed (%d)", err);
            return err;
        }
    }

    return 0;
}

int composition_root_stop(app_container_t *self)
{
    if (self == NULL)
    {
        return -EINVAL;
    }

    for (uint8_t bus = 0U; bus < (uint8_t)MODBUS_BUS_MAX; bus++)
    {
        (void)ao_modbus_stop(&self->ao[bus]);
    }

    if ((self->init_mask & APP_CONTAINER_INIT_STORAGE) != 0U)
    {
        (void)ao_storage_stop(&self->ao_store);
    }

    if ((self->init_mask & APP_CONTAINER_INIT_NETWORK) != 0U)
    {
        (void)ao_network_stop(&self->ao_net);
    }

    return 0;
}

