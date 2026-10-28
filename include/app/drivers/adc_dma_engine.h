/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_DRIVERS_ADC_DMA_ENGINE_H_
#define APP_DRIVERS_ADC_DMA_ENGINE_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Public operations vtable for Multi-ADC DMA Parallel Engine.
 */
struct adc_dma_driver_api {
    int (*trigger_scan)(const struct device *dev);
    int (*wait_scan)(const struct device *dev, k_timeout_t timeout);
    int (*get_raw_sample)(const struct device *dev, uint8_t channel_idx, int16_t *out_counts);
    size_t (*get_channel_count)(const struct device *dev);
};

static inline int adc_dma_trigger_scan(const struct device *dev)
{
    const struct adc_dma_driver_api *api = (const struct adc_dma_driver_api *)dev->api;
    if (api == NULL || api->trigger_scan == NULL) {
        return -ENOSYS;
    }
    return api->trigger_scan(dev);
}

static inline int adc_dma_wait_scan(const struct device *dev, k_timeout_t timeout)
{
    const struct adc_dma_driver_api *api = (const struct adc_dma_driver_api *)dev->api;
    if (api == NULL || api->wait_scan == NULL) {
        return -ENOSYS;
    }
    return api->wait_scan(dev, timeout);
}

static inline int adc_dma_get_raw_sample(const struct device *dev, uint8_t channel_idx, int16_t *out_counts)
{
    const struct adc_dma_driver_api *api = (const struct adc_dma_driver_api *)dev->api;
    if (api == NULL || api->get_raw_sample == NULL) {
        return -ENOSYS;
    }
    return api->get_raw_sample(dev, channel_idx, out_counts);
}

static inline size_t adc_dma_get_channel_count(const struct device *dev)
{
    const struct adc_dma_driver_api *api = (const struct adc_dma_driver_api *)dev->api;
    if (api == NULL || api->get_channel_count == NULL) {
        return 0;
    }
    return api->get_channel_count(dev);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_DRIVERS_ADC_DMA_ENGINE_H_ */
