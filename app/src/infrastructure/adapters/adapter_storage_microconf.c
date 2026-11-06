/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Concrete storage adapter implementing i_config_storage_api_t using microconf
 * and the Zephyr EEPROM driver.
 */

#include "infrastructure/adapters/adapter_storage_microconf.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_storage_mconf, LOG_LEVEL_INF);

/* 32-byte header size used by microconf */
#define MCONF_STORAGE_HEADER_SIZE 32U

static const char *s_domain_keys[CONFIG_DOMAIN_MAX] = {
    [CONFIG_DOMAIN_DEVICE]      = "device",
    [CONFIG_DOMAIN_MODBUS]      = "modbus",
    [CONFIG_DOMAIN_NET_ETH]     = "net_eth",
    [CONFIG_DOMAIN_NET_WIFI]    = "net_wifi",
    [CONFIG_DOMAIN_CALIBRATION] = "calib",
    [CONFIG_DOMAIN_SENSOR_CFG]  = "sensors",
    [CONFIG_DOMAIN_BASELINE]    = "baseline",
    [CONFIG_DOMAIN_HVAC]        = "hvac",
};

static int mconf_err_to_errno(mconf_err_t err)
{
    switch (err) {
    case MCONF_OK:
        return 0;
    case MCONF_ERR_NULL:
    case MCONF_ERR_RANGE:
    case MCONF_ERR_UNINITIALIZED:
        return -EINVAL;
    case MCONF_ERR_NOT_FOUND:
        return -ENOENT;
    case MCONF_ERR_SIZE:
        return -ENOMEM;
    case MCONF_ERR_CRC:
    case MCONF_ERR_MAGIC:
    case MCONF_ERR_VERSION:
        return -ENOENT;
    case MCONF_ERR_IO:
    case MCONF_ERR_INVALID:
    default:
        return -EIO;
    }
}

static int storage_api_init(void *impl)
{
    adapter_storage_microconf_t *ctx = (adapter_storage_microconf_t *)impl;

    if (ctx == NULL || ctx->eeprom_dev == NULL) {
        return -EINVAL;
    }

    if (!device_is_ready(ctx->eeprom_dev)) {
        LOG_ERR("EEPROM device '%s' is not ready", ctx->eeprom_dev->name);
        return -ENODEV;
    }

    for (int d = 0; d < (int)CONFIG_DOMAIN_MAX; d++) {
        config_domain_t domain = (config_domain_t)d;
        size_t base_offset = 0;
        size_t total_size = 0;
        size_t slot_size = 0;

        int ret = config_domain_get_layout(domain, &base_offset, &total_size, &slot_size);
        if (ret != 0) {
            LOG_ERR("Failed to get layout for domain %d: %d", d, ret);
            return ret;
        }

        size_t payload_capacity = slot_size - MCONF_STORAGE_HEADER_SIZE;
        if (payload_capacity > ADAPTER_STORAGE_MICROCONF_MAX_PAYLOAD) {
            LOG_ERR("Domain %d payload capacity (%zu) exceeds static buffer (%u)",
                    d, payload_capacity, ADAPTER_STORAGE_MICROCONF_MAX_PAYLOAD);
            return -ENOMEM;
        }

        ctx->domain_payload_size[d] = payload_capacity;

        ret = mconf_zephyr_eeprom_init(&ctx->eeprom_ctx[d],
                                       ctx->eeprom_dev,
                                       base_offset,
                                       total_size,
                                       &ctx->io[d]);
        if (ret != 0) {
            LOG_ERR("mconf_zephyr_eeprom_init failed for domain %d: %d", d, ret);
            return ret;
        }

        /* Set up BLOB schema entry */
        ctx->entries[d].key = s_domain_keys[d];
        ctx->entries[d].offset = 0;
        ctx->entries[d].size = payload_capacity;
        ctx->entries[d].type = MCONF_TYPE_BLOB;
        ctx->entries[d].default_value = NULL;
        ctx->entries[d].default_size = 0;

        ctx->schemas[d].entries = &ctx->entries[d];
        ctx->schemas[d].entry_count = 1U;
        ctx->schemas[d].schema_version = MCONF_SCHEMA_VERSION_CURRENT;
        ctx->schemas[d].data_size = payload_capacity;

        memset(ctx->payload_buf[d], 0, sizeof(ctx->payload_buf[d]));

        mconf_err_t merr = mconf_init(&ctx->mconf[d],
                                      sizeof(ctx->mconf[d]),
                                      &ctx->schemas[d],
                                      ctx->payload_buf[d],
                                      payload_capacity);
        if (merr != MCONF_OK) {
            LOG_ERR("mconf_init failed for domain %d: %d (%s)",
                    d, merr, mconf_err_str(merr));
            return mconf_err_to_errno(merr);
        }
    }

    ctx->is_initialized = true;
    LOG_INF("adapter_storage_microconf initialized successfully for all %d domains", CONFIG_DOMAIN_MAX);
    return 0;
}

static int storage_api_save(void *impl, config_domain_t domain, const void *data, size_t size)
{
    adapter_storage_microconf_t *ctx = (adapter_storage_microconf_t *)impl;

    if (ctx == NULL || !ctx->is_initialized) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }
    if (data == NULL || size == 0) {
        return -EINVAL;
    }
    if (size > ctx->domain_payload_size[domain]) {
        LOG_ERR("Domain %d save data size (%zu) exceeds capacity (%zu)",
                domain, size, ctx->domain_payload_size[domain]);
        return -EFBIG;
    }

    /* Zero out the unused payload area to keep storage clean and deterministic */
    memset(ctx->payload_buf[domain], 0, ctx->domain_payload_size[domain]);
    memcpy(ctx->payload_buf[domain], data, size);

    mconf_err_t merr = mconf_save(&ctx->mconf[domain], &ctx->io[domain]);
    if (merr != MCONF_OK) {
        LOG_ERR("mconf_save failed for domain %d: %d (%s)",
                domain, merr, mconf_err_str(merr));
        return mconf_err_to_errno(merr);
    }

    LOG_DBG("Domain %d successfully persisted (%zu bytes)", domain, size);
    return 0;
}

static int storage_api_load(void *impl, config_domain_t domain, void *out_data,
                            size_t max_size, size_t *out_size)
{
    adapter_storage_microconf_t *ctx = (adapter_storage_microconf_t *)impl;

    if (ctx == NULL || !ctx->is_initialized) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }
    if (out_data == NULL || max_size == 0) {
        return -EINVAL;
    }

    mconf_err_t merr = mconf_load(&ctx->mconf[domain], &ctx->io[domain]);
    if (merr != MCONF_OK) {
        LOG_DBG("mconf_load returned %d (%s) for domain %d",
                merr, mconf_err_str(merr), domain);
        return mconf_err_to_errno(merr);
    }

    size_t copy_size = ctx->domain_payload_size[domain];
    if (copy_size > max_size) {
        copy_size = max_size;
    }

    memcpy(out_data, ctx->payload_buf[domain], copy_size);
    if (out_size != NULL) {
        *out_size = copy_size;
    }

    return 0;
}

static int storage_api_reset_defaults(void *impl, config_domain_t domain)
{
    adapter_storage_microconf_t *ctx = (adapter_storage_microconf_t *)impl;

    if (ctx == NULL || !ctx->is_initialized) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    mconf_err_t merr = mconf_load_defaults(&ctx->mconf[domain]);
    if (merr != MCONF_OK) {
        LOG_ERR("mconf_load_defaults failed for domain %d: %d (%s)",
                domain, merr, mconf_err_str(merr));
        return mconf_err_to_errno(merr);
    }

    merr = mconf_save(&ctx->mconf[domain], &ctx->io[domain]);
    if (merr != MCONF_OK) {
        LOG_ERR("mconf_save after defaults failed for domain %d: %d (%s)",
                domain, merr, mconf_err_str(merr));
        return mconf_err_to_errno(merr);
    }

    LOG_INF("Domain %d reset to defaults and saved", domain);
    return 0;
}

static const i_config_storage_api_t s_storage_microconf_api = {
    .init           = storage_api_init,
    .save           = storage_api_save,
    .load           = storage_api_load,
    .reset_defaults = storage_api_reset_defaults,
};

int adapter_storage_microconf_init(adapter_storage_microconf_t *ctx,
                                   const struct device *eeprom_dev)
{
    if (ctx == NULL || eeprom_dev == NULL) {
        return -EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->eeprom_dev = eeprom_dev;
    ctx->is_initialized = false;

    return storage_api_init(ctx);
}

i_config_storage_t adapter_storage_microconf_port(adapter_storage_microconf_t *ctx)
{
    i_config_storage_t port;

    if (ctx == NULL) {
        port.api = NULL;
        port.impl = NULL;
    } else {
        port.api = &s_storage_microconf_api;
        port.impl = ctx;
    }

    return port;
}
