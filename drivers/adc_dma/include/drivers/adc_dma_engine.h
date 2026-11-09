/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public header for the Multi-ADC DMA Parallel Engine driver (STM32H7).
 */

#ifndef DRIVERS_ADC_DMA_ENGINE_H_
#define DRIVERS_ADC_DMA_ENGINE_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver API for the ADC DMA engine.
 */
struct adc_dma_driver_api {
    /**
     * @brief Trigger an asynchronous ADC scan across all configured groups.
     *
     * @param dev Driver device instance.
     * @return 0 on success, negative errno on failure.
     */
    int (*trigger_scan)(const struct device *dev);

    /**
     * @brief Wait for all ADC groups to complete their DMA transfer.
     *
     * @param dev   Driver device instance.
     * @param timeout Maximum time to wait.
     * @return 0 on success, negative errno on failure.
     */
    int (*wait_scan)(const struct device *dev, k_timeout_t timeout);

    /**
     * @brief Retrieve a raw ADC sample count for a logical channel index.
     *
     * @param dev        Driver device instance.
     * @param channel_idx Logical channel index (0..total_channels-1).
     * @param out_counts Output destination for raw ADC counts.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_raw_sample)(const struct device *dev, uint8_t channel_idx, uint16_t *out_counts);

    /**
     * @brief Return the total number of configured logical channels.
     *
     * @param dev Driver device instance.
     * @return Total channel count.
     */
    size_t (*get_channel_count)(const struct device *dev);
};

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_ADC_DMA_ENGINE_H_ */
