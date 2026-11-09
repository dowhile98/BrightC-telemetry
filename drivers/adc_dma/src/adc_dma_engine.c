/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multi-ADC DMA Parallel Engine Driver for STM32H7.
 *
 * One driver instance owns up to three ADC controllers. The channels listed in
 * the `io-channels` devicetree property define a dense logical index space:
 * logical index i is the i-th entry of that property. Logical indices are
 * grouped per controller and each group is started with a single
 * adc_read_async() call, so every controller samples its channels in parallel.
 */

#define DT_DRV_COMPAT daecge_adc_dma_engine

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <drivers/adc_dma_engine.h>

#include <string.h>

LOG_MODULE_REGISTER(adc_dma_engine, CONFIG_ADC_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
             "Only one adc-dma-engine instance is supported");

#define MAX_ADC_CONTROLLERS   3
#define MAX_CHANNELS_PER_ADC  16
#define MAX_TOTAL_CHANNELS    (MAX_ADC_CONTROLLERS * MAX_CHANNELS_PER_ADC)

/** Channels physically supported by an STM32H7 ADC sequence mask. */
#define MAX_ADC_HW_CHANNEL_ID 31U

/** State of one ADC controller participating in the parallel scan. */
struct adc_ctrl_group {
    const struct device *adc_dev;
    uint32_t channel_mask;
    uint8_t channel_ids[MAX_CHANNELS_PER_ADC]; /**< Ascending: equals DMA buffer order */
    uint8_t num_channels;
    int16_t *dma_buf;                          /**< Points into s_adc_dma_arena */
    struct k_poll_signal done_signal;
};

struct adc_dma_data {
    struct adc_ctrl_group groups[MAX_ADC_CONTROLLERS];
    size_t active_groups;
    size_t total_channels;
    uint8_t channel_to_group[MAX_TOTAL_CHANNELS];
    uint8_t channel_to_buf_idx[MAX_TOTAL_CHANNELS];
    uint8_t channel_input[MAX_TOTAL_CHANNELS];
};

/*
 * DMA destinations must stay out of the data cache: on the STM32H7 the DMA
 * writes them behind the CPU's back, so a cached buffer would return stale
 * counts. Section attributes cannot be applied to struct members, hence the
 * arena lives at file scope; a single engine instance is enforced above.
 */
static int16_t s_adc_dma_arena[MAX_ADC_CONTROLLERS][MAX_CHANNELS_PER_ADC]
    __aligned(32) __nocache;

struct adc_dma_config {
    uint8_t resolution;
};

/**
 * @brief Locate the group that owns a controller, creating it when needed.
 *
 * @return Group index, or -ENOMEM when all controller slots are taken.
 */
static int adc_dma_engine_group_for(struct adc_dma_data *data,
                                    const struct device *adc_dev)
{
    for (size_t g = 0U; g < data->active_groups; g++) {
        if (data->groups[g].adc_dev == adc_dev) {
            return (int)g;
        }
    }

    if (data->active_groups >= MAX_ADC_CONTROLLERS) {
        return -ENOMEM;
    }

    struct adc_ctrl_group *grp = &data->groups[data->active_groups];
    grp->adc_dev = adc_dev;
    grp->channel_mask = 0U;
    grp->num_channels = 0U;
    grp->dma_buf = s_adc_dma_arena[data->active_groups];

    (void)memset(grp->channel_ids, 0, sizeof(grp->channel_ids));
    k_poll_signal_init(&grp->done_signal);

    return (int)data->active_groups++;
}

static int adc_dma_engine_trigger_scan(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    const struct adc_dma_config *cfg = dev->config;

    for (size_t i = 0; i < data->active_groups; i++) {
        struct adc_ctrl_group *grp = &data->groups[i];
        k_poll_signal_reset(&grp->done_signal);

        struct adc_sequence seq = {
            .channels = grp->channel_mask,
            .buffer = grp->dma_buf,
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

    k_timepoint_t timepoint = sys_timepoint_calc(timeout);
    int ret = 0;

    for (size_t i = 0; i < data->active_groups; i++) {
        struct k_poll_event ev = K_POLL_EVENT_INITIALIZER(
            K_POLL_TYPE_SIGNAL,
            K_POLL_MODE_NOTIFY_ONLY,
            &data->groups[i].done_signal
        );

        k_timeout_t remaining = sys_timepoint_timeout(timepoint);
        ret = k_poll(&ev, 1, remaining);
        if (ret != 0) {
            LOG_WRN("ADC DMA wait group %u failed or timed out: %d", (unsigned int)i, ret);
            break;
        }

        unsigned int signaled = 0;
        int result = 0;
        k_poll_signal_check(&data->groups[i].done_signal, &signaled, &result);
        if (result != 0) {
            LOG_WRN("ADC DMA group %u completed with error: %d", (unsigned int)i, result);
            ret = result;
            break;
        }
    }

    if (ret != 0) {
        for (size_t i = 0; i < data->active_groups; i++) {
            k_poll_signal_reset(&data->groups[i].done_signal);
        }
        return ret;
    }

    return 0;
}

static int adc_dma_engine_get_raw_sample(const struct device *dev, uint8_t channel_idx,
                                         int16_t *out_counts)
{
    struct adc_dma_data *data = dev->data;
    if (out_counts == NULL || channel_idx >= data->total_channels) {
        return -EINVAL;
    }

    uint8_t grp_idx = data->channel_to_group[channel_idx];
    uint8_t buf_idx = data->channel_to_buf_idx[channel_idx];

    *out_counts = data->groups[grp_idx].dma_buf[buf_idx];
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

/**
 * @brief Describe one `io-channels` entry at compile time.
 *
 * DT_FOREACH_PROP_ELEM() supplies a node identifier, so the node-based
 * phandle-array accessors are used here (the DT_INST_* variants take an
 * instance number instead). The resulting table order is the logical channel
 * index space used by the runtime API.
 */
struct adc_channel_desc {
    const struct device *adc_dev;
    uint8_t channel;
};

#define ADC_DMA_CHANNEL_DESC(node_id, prop, idx)                                  \
    {                                                                             \
        .adc_dev = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR_BY_IDX(node_id, idx)),        \
        .channel = (uint8_t)DT_IO_CHANNELS_INPUT_BY_IDX(node_id, idx),             \
    },

BUILD_ASSERT(DT_INST_PROP_LEN(0, io_channels) > 0,
             "adc-dma-engine requires at least one io-channels entry");
BUILD_ASSERT(DT_INST_PROP_LEN(0, io_channels) <= MAX_TOTAL_CHANNELS,
             "io-channels exceeds the engine capacity");

static const struct adc_channel_desc s_adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_DRV_INST(0), io_channels, ADC_DMA_CHANNEL_DESC)
};

/**
 * @brief Build the logical channel map from the `io-channels` property.
 *
 * Logical index i corresponds to the i-th `io-channels` entry. Each entry is
 * appended to its controller group in ascending channel order, which is the
 * order the STM32 ADC writes into the DMA buffer.
 */
static int adc_dma_engine_build_map(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    const size_t n = ARRAY_SIZE(s_adc_channels);

    for (size_t i = 0U; i < n; i++) {
        const struct device *adc_dev = s_adc_channels[i].adc_dev;
        const uint32_t channel = s_adc_channels[i].channel;

        if (channel > MAX_ADC_HW_CHANNEL_ID) {
            LOG_ERR("io-channels[%u]: channel %u out of range",
                    (unsigned int)i, (unsigned int)channel);
            return -EINVAL;
        }

        if (!device_is_ready(adc_dev)) {
            LOG_ERR("io-channels[%u]: controller %s not ready",
                    (unsigned int)i, adc_dev->name);
            return -ENODEV;
        }

        struct adc_channel_cfg ch_cfg = {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 388),
            .channel_id = channel,
            .differential = 0,
        };
        int ch_err = adc_channel_setup(adc_dev, &ch_cfg);
        if (ch_err != 0) {
            LOG_ERR("io-channels[%u]: adc_channel_setup ch %u on %s failed (%d)",
                    (unsigned int)i, (unsigned int)channel, adc_dev->name, ch_err);
            return ch_err;
        }

        const int g = adc_dma_engine_group_for(data, adc_dev);
        if (g < 0) {
            return g;
        }

        struct adc_ctrl_group *grp = &data->groups[g];

        if ((grp->channel_mask & (uint32_t)BIT(channel)) != 0U) {
            LOG_ERR("io-channels[%u]: channel %u listed twice on %s",
                    (unsigned int)i, (unsigned int)channel, adc_dev->name);
            return -EINVAL;
        }

        if (grp->num_channels >= MAX_CHANNELS_PER_ADC) {
            LOG_ERR("controller %s exceeds %u channels",
                    adc_dev->name, MAX_CHANNELS_PER_ADC);
            return -ENOMEM;
        }

        grp->channel_mask |= (uint32_t)BIT(channel);

        /* Insert in ascending order to mirror the DMA buffer layout. */
        size_t pos = grp->num_channels;
        while (pos > 0U && grp->channel_ids[pos - 1U] > (uint8_t)channel) {
            grp->channel_ids[pos] = grp->channel_ids[pos - 1U];
            pos--;
        }
        grp->channel_ids[pos] = (uint8_t)channel;
        grp->num_channels++;

        data->channel_to_group[i] = (uint8_t)g;
        data->channel_input[i] = (uint8_t)channel;
    }

    data->total_channels = n;

    /* Resolve the DMA buffer slot of every logical index. */
    for (size_t i = 0U; i < n; i++) {
        const struct adc_ctrl_group *grp = &data->groups[data->channel_to_group[i]];

        for (size_t p = 0U; p < grp->num_channels; p++) {
            if (grp->channel_ids[p] == data->channel_input[i]) {
                data->channel_to_buf_idx[i] = (uint8_t)p;
                break;
            }
        }
    }

    return 0;
}

static int adc_dma_engine_init(const struct device *dev)
{
    struct adc_dma_data *data = dev->data;
    const struct adc_dma_config *cfg = dev->config;

    LOG_INF("Initializing Multi-ADC DMA Engine (Resolution: %u bits)", cfg->resolution);

    data->active_groups = 0U;
    data->total_channels = 0U;

    int err = adc_dma_engine_build_map(dev);
    if (err != 0) {
        return err;
    }

    LOG_INF("ADC DMA engine ready: %u controllers, %u logical channels",
            (unsigned int)data->active_groups, (unsigned int)data->total_channels);

    return 0;
}

#define ADC_DMA_ENGINE_INIT(inst)                                                  \
    static struct adc_dma_data adc_dma_data_##inst;                                \
    static const struct adc_dma_config adc_dma_config_##inst = {                   \
        .resolution = DT_INST_PROP_OR(inst, resolution, 16),                       \
    };                                                                             \
    DEVICE_DT_INST_DEFINE(inst,                                                    \
                          adc_dma_engine_init,                                     \
                          NULL,                                                    \
                          &adc_dma_data_##inst,                                    \
                          &adc_dma_config_##inst,                                  \
                          POST_KERNEL,                                             \
                          CONFIG_APPLICATION_INIT_PRIORITY,                        \
                          &adc_dma_engine_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_DMA_ENGINE_INIT)
