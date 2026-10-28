/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multi-ADC DMA Parallel Engine Driver for STM32H7.
 */

#define DT_DRV_COMPAT daecge_adc_dma_engine

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <app/drivers/adc_dma_engine.h>

LOG_MODULE_REGISTER(adc_dma_engine, CONFIG_ADC_LOG_LEVEL);

#define MAX_ADC_CONTROLLERS 3
#define MAX_CHANNELS_PER_ADC 16
#define MAX_TOTAL_CHANNELS (MAX_ADC_CONTROLLERS * MAX_CHANNELS_PER_ADC)

struct adc_ctrl_group {
    const struct device *adc_dev;
    uint32_t channel_mask;
    uint8_t channel_ids[MAX_CHANNELS_PER_ADC];
    uint8_t num_channels;
    struct k_poll_signal done_signal;
};

struct adc_dma_data {
    struct adc_ctrl_group groups[MAX_ADC_CONTROLLERS];
    size_t active_groups;
    size_t total_channels;
    uint8_t channel_to_group[MAX_TOTAL_CHANNELS];
    uint8_t channel_to_buf_idx[MAX_TOTAL_CHANNELS];
    
    /* DMA sample buffers placed in non-cacheable RAM for STM32H7 */
    int16_t dma_buf_group0[MAX_CHANNELS_PER_ADC] __aligned(32) __nocache;
    int16_t dma_buf_group1[MAX_CHANNELS_PER_ADC] __aligned(32) __nocache;
    int16_t dma_buf_group2[MAX_CHANNELS_PER_ADC] __aligned(32) __nocache;
};

struct adc_dma_config {
    uint8_t resolution;
};

static int adc_dma_engine_trigger_scan(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    const struct adc_dma_config *cfg = dev->config;

    for (size_t i = 0; i < data->active_groups; i++) {
        struct adc_ctrl_group *grp = &data->groups[i];
        k_poll_signal_reset(&grp->done_signal);

        int16_t *buf = (i == 0) ? data->dma_buf_group0 : 
                       (i == 1) ? data->dma_buf_group1 : data->dma_buf_group2;

        struct adc_sequence seq = {
            .channels = grp->channel_mask,
            .buffer = buf,
            .buffer_size = grp->num_channels * sizeof(int16_t),
            .resolution = cfg->resolution,
        };

        int err = adc_read_async(grp->adc_dev, &seq, &grp->done_signal);
        if (err) {
            LOG_ERR("adc_read_async group %u failed: %d", (unsigned int)i, err);
            return err;
        }
    }

    return 0;
}

static int adc_dma_engine_wait_scan(const struct device *dev, k_timeout_t timeout)
{
    struct adc_dma_data *data = dev->data;
    if (data->active_groups == 0) {
        return 0;
    }

    struct k_poll_event evs[MAX_ADC_CONTROLLERS];
    for (size_t i = 0; i < data->active_groups; i++) {
        evs[i] = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
            K_POLL_TYPE_SIGNAL,
            K_POLL_MODE_NOTIFY_ONLY,
            &data->groups[i].done_signal
        );
    }

    int ret = k_poll(evs, data->active_groups, timeout);
    if (ret != 0) {
        LOG_WRN("ADC DMA wait timeout or error: %d", ret);
        return ret;
    }

    return 0;
}

static int adc_dma_engine_get_raw_sample(const struct device *dev, uint8_t channel_idx, int16_t *out_counts)
{
    struct adc_dma_data *data = dev->data;
    if (out_counts == NULL || channel_idx >= data->total_channels) {
        return -EINVAL;
    }

    uint8_t grp_idx = data->channel_to_group[channel_idx];
    uint8_t buf_idx = data->channel_to_buf_idx[channel_idx];

    int16_t *buf = (grp_idx == 0) ? data->dma_buf_group0 : 
                   (grp_idx == 1) ? data->dma_buf_group1 : data->dma_buf_group2;

    *out_counts = buf[buf_idx];
    return 0;
}

static size_t adc_dma_engine_get_channel_count(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    return data->total_channels;
}

static const struct adc_dma_driver_api adc_dma_engine_api = {
    .trigger_scan = adc_dma_engine_trigger_scan,
    .wait_scan = adc_dma_engine_wait_scan,
    .get_raw_sample = adc_dma_engine_get_raw_sample,
    .get_channel_count = adc_dma_engine_get_channel_count,
};

static int adc_dma_engine_init(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    const struct adc_dma_config *cfg = dev->config;

    LOG_INF("Initializing Multi-ADC DMA Engine (Resolution: %u bits)", cfg->resolution);

    data->active_groups = 0;
    data->total_channels = 0;

    /* Check for ADC1, ADC2, ADC3 device readiness if enabled */
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(adc1))
    const struct device *adc1_dev = DEVICE_DT_GET(DT_NODELABEL(adc1));
    if (device_is_ready(adc1_dev)) {
        size_t g = data->active_groups++;
        data->groups[g].adc_dev = adc1_dev;
        k_poll_signal_init(&data->groups[g].done_signal);
        LOG_INF("Registered ADC1 into DMA engine (Group %u)", (unsigned int)g);
    }
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(adc2))
    const struct device *adc2_dev = DEVICE_DT_GET(DT_NODELABEL(adc2));
    if (device_is_ready(adc2_dev)) {
        size_t g = data->active_groups++;
        data->groups[g].adc_dev = adc2_dev;
        k_poll_signal_init(&data->groups[g].done_signal);
        LOG_INF("Registered ADC2 into DMA engine (Group %u)", (unsigned int)g);
    }
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(adc3))
    const struct device *adc3_dev = DEVICE_DT_GET(DT_NODELABEL(adc3));
    if (device_is_ready(adc3_dev)) {
        size_t g = data->active_groups++;
        data->groups[g].adc_dev = adc3_dev;
        k_poll_signal_init(&data->groups[g].done_signal);
        LOG_INF("Registered ADC3 into DMA engine (Group %u)", (unsigned int)g);
    }
#endif

    return 0;
}

#define ADC_DMA_ENGINE_INIT(inst)                                                  static struct adc_dma_data adc_dma_data_##inst;                               static const struct adc_dma_config adc_dma_config_##inst = {                      .resolution = DT_INST_PROP_OR(inst, resolution, 16),                       };                                                                             DEVICE_DT_INST_DEFINE(inst,                                                                          adc_dma_engine_init,                                                           NULL,                                                                          &adc_dma_data_##inst,                                                          &adc_dma_config_##inst,                                                        POST_KERNEL,                                                                   CONFIG_APPLICATION_INIT_PRIORITY,                                              &adc_dma_engine_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_DMA_ENGINE_INIT)
