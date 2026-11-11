/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Concrete persistent storage adapter using microconf and Zephyr EEPROM driver.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_MICROCONF_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_MICROCONF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>

#include <mconf.h>
#include <mconf_zephyr.h>

#include "common/config_types.h"
#include "interfaces/i_config_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum payload size in bytes per slot across any domain (768 - 32 = 736 B). */
#define ADAPTER_STORAGE_MICROCONF_MAX_PAYLOAD 736U

/**
 * @brief Context structure for the microconf Zephyr EEPROM storage adapter.
 *
 * Stored by value in app_container_t. Zero dynamic memory allocation.
 */
typedef struct {
    const struct device *eeprom_dev;
    bool is_initialized;

    /* Microconf resources for all logical domains */
    mconf_entry_t                  entries[CONFIG_DOMAIN_MAX];
    mconf_schema_t                 schemas[CONFIG_DOMAIN_MAX];
    mconf_t                        mconf[CONFIG_DOMAIN_MAX];
    mconf_io_t                     io[CONFIG_DOMAIN_MAX];
    struct mconf_zephyr_eeprom_ctx eeprom_ctx[CONFIG_DOMAIN_MAX];
    size_t                         domain_payload_size[CONFIG_DOMAIN_MAX];
    uint8_t                        payload_buf[CONFIG_DOMAIN_MAX][ADAPTER_STORAGE_MICROCONF_MAX_PAYLOAD];
} adapter_storage_microconf_t;

/**
 * @brief Initialize the concrete storage adapter with the given EEPROM device.
 *
 * @param[out] ctx         Adapter context instance.
 * @param[in]  eeprom_dev  Zephyr EEPROM device pointer.
 * @return 0 on success, negative errno on failure.
 */
int adapter_storage_microconf_init(adapter_storage_microconf_t *ctx,
                                   const struct device *eeprom_dev);

/**
 * @brief Produce an i_config_storage_t port handle bound to this adapter instance.
 *
 * @param[in] ctx Adapter context instance.
 * @return Bound i_config_storage_t port handle.
 */
i_config_storage_t adapter_storage_microconf_port(adapter_storage_microconf_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_MICROCONF_H_ */
