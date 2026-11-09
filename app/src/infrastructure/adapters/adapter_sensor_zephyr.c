/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infrastructure adapter: Sensor Matrix Ingestion over Zephyr ADC DMA.
 *
 * Acquires raw ADC counts from the multi-ADC DMA engine and hands them to the
 * pure domain `sensor_matrix_map()`, which owns every physical-to-engineering
 * conversion (Callendar-Van Dusen, 4-20 mA scaling, calibration, validity
 * bounds and semantic routing). This adapter performs no math on the samples:
 * it only translates bus indices into physical channel identifiers.
 */

#include "adapter_sensor_zephyr.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/adc_dma_engine.h>

#include "domain/services/sensor_matrix_mapper.h"

LOG_MODULE_REGISTER(adapter_sensor_zephyr, LOG_LEVEL_INF);

/** Maximum time to wait for an ADC DMA scan to complete. */
#define ADC_DMA_SCAN_TIMEOUT_MS 1000U

/** Default acquisition period when not configured. */
#define ADC_DMA_DEFAULT_SCAN_INTERVAL_MS 500U

/** Number of consecutive acquisition failures before invalidating readings. */
#define ADC_CONSECUTIVE_FAILURES_THRESHOLD 3U

/** Number of on-board RTD channels wired to the ADC stage. */
#define ADC_RTD_CHANNEL_COUNT 5U

/** Number of J9 4-20 mA analog inputs wired to the ADC stage. */
#define ADC_J9_CHANNEL_COUNT 10U

/**
 * @brief Map a logical ADC DMA channel index to a physical_channel_id_t.
 *
 * The ADC DMA engine exposes dense logical indices. This table reflects the
 * BrightC V3 wiring: indices 0..4 are the on-board RTD stage, indices 5..14
 * are the J9 4-20 mA inputs.
 *
 * @param[in] adc_idx Logical ADC DMA channel index.
 * @return The corresponding physical_channel_id_t, or PHYS_CH_MAX if unmapped.
 */
static physical_channel_id_t adc_idx_to_phys_ch(size_t adc_idx)
{
    if (adc_idx < ADC_RTD_CHANNEL_COUNT) {
        return (physical_channel_id_t)(PHYS_CH_INTERNAL_RTD_1 + adc_idx);
    }
    if (adc_idx < (ADC_RTD_CHANNEL_COUNT + ADC_J9_CHANNEL_COUNT)) {
        return (physical_channel_id_t)(PHYS_CH_ANALOG_J9_CH1 +
                                       (adc_idx - ADC_RTD_CHANNEL_COUNT));
    }
    return PHYS_CH_MAX;
}

/**
 * @brief Acquire raw ADC counts for every RTD and J9 analog channel.
 *
 * Only the "sample was delivered" condition is recorded in the validity
 * masks. Physical plausibility (open probes, NAMUR NE43 range, temperature
 * bounds) is evaluated by `sensor_matrix_map()`, keeping a single source of
 * truth for the engineering rules.
 *
 * @param[in]  self Adapter instance.
 * @param[out] raw  Raw readings bank to populate.
 * @return 0 on success, negative errno on failure.
 */
static int acquire_adc_samples(adapter_sensor_zephyr_t *self, raw_sensor_readings_t *raw)
{
    if (self == NULL || raw == NULL || self->adc_api == NULL || self->adc_dma_dev == NULL) {
        return -EINVAL;
    }

    int err = self->adc_api->trigger_scan(self->adc_dma_dev);
    if (err != 0) {
        LOG_ERR("ADC DMA trigger_scan failed (%d)", err);
        return err;
    }

    err = self->adc_api->wait_scan(self->adc_dma_dev, K_MSEC(ADC_DMA_SCAN_TIMEOUT_MS));
    if (err != 0) {
        LOG_WRN("ADC DMA wait_scan failed (%d)", err);
        return err;
    }

    const size_t total_ch = self->adc_api->get_channel_count(self->adc_dma_dev);

    raw->internal_rtd_valid_mask = 0U;
    raw->analog_j9_valid_mask = 0U;

    for (size_t i = 0U; i < total_ch; i++) {
        const physical_channel_id_t phys_ch = adc_idx_to_phys_ch(i);
        if (phys_ch == PHYS_CH_MAX) {
            continue;
        }

        int16_t counts = 0;
        err = self->adc_api->get_raw_sample(self->adc_dma_dev, (uint8_t)i, &counts);
        if (err != 0) {
            LOG_WRN("ADC DMA get_raw_sample ch %u failed (%d)", (unsigned int)i, err);
            continue;
        }

        if (phys_ch >= PHYS_CH_INTERNAL_RTD_1 && phys_ch <= PHYS_CH_INTERNAL_RTD_5) {
            const size_t rtd_idx = (size_t)(phys_ch - PHYS_CH_INTERNAL_RTD_1);
            raw->internal_rtd_raw_adc[rtd_idx] = (float)counts;
            raw->internal_rtd_valid_mask |= (uint8_t)(1U << rtd_idx);
        } else {
            const size_t j9_idx = (size_t)(phys_ch - PHYS_CH_ANALOG_J9_CH1);
            raw->analog_j9_raw_adc[j9_idx] = (float)counts;
            raw->analog_j9_valid_mask |= (uint16_t)(1U << j9_idx);
        }
    }

    return 0;
}

/*============================================================================*
 * PORT V-TABLE IMPLEMENTATION
 *============================================================================*/

static int sensor_zephyr_update(void *impl)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL) {
        return -EINVAL;
    }

    int err = acquire_adc_samples(self, &self->raw);
    if (err != 0) {
        if (self->consecutive_failures < UINT8_MAX) {
            self->consecutive_failures++;
        }
        LOG_WRN("ADC sample acquisition failed (consecutive: %u, err: %d)",
                (unsigned int)self->consecutive_failures, err);

        if (self->consecutive_failures >= ADC_CONSECUTIVE_FAILURES_THRESHOLD) {
            /* Debounce threshold exceeded: invalidate masks */
            self->raw.internal_rtd_valid_mask = 0U;
            self->raw.analog_j9_valid_mask = 0U;
        }
        /* Else keep last known good readings and valid masks across transient glitches */
    } else {
        if (self->consecutive_failures > 0U) {
            LOG_INF("ADC sample acquisition recovered after %u failures",
                    (unsigned int)self->consecutive_failures);
        }
        self->consecutive_failures = 0U;
    }

    self->raw.timestamp_us = (uint64_t)k_uptime_get() * 1000ULL;

    err = sensor_matrix_map(&self->raw,
                            self->channel_cfg,
                            self->channel_cfg_count,
                            &self->frame);
    if (err != 0) {
        LOG_ERR("sensor_matrix_map failed (%d)", err);
        return err;
    }

    self->frame_valid = true;

    LOG_DBG("sensor matrix updated: %u pairs, %u room, %u outdoor",
            self->frame.configured_pairs,
            self->frame.configured_room_probes,
            self->frame.configured_outdoor_probes);

    return 0;
}

static int sensor_zephyr_get_frame(void *impl, thermal_matrix_frame_t *out_frame)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL || out_frame == NULL) {
        return -EINVAL;
    }

    if (!self->frame_valid) {
        return -ENODATA;
    }

    *out_frame = self->frame;
    return 0;
}

static int sensor_zephyr_get_raw_readings(void *impl, raw_sensor_readings_t *out_raw)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL || out_raw == NULL) {
        return -EINVAL;
    }

    *out_raw = self->raw;
    return 0;
}

static int sensor_zephyr_submit_cwt_bank(void *impl,
                                         uint8_t concentrator,
                                         const temperature_bank_16ch_t *bank)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL || bank == NULL || concentrator >= SENSOR_MAX_CWT_CONCENTRATORS) {
        return -EINVAL;
    }

    memcpy(self->raw.cwt_temperatures_c[concentrator],
           bank->temperatures_c,
           sizeof(self->raw.cwt_temperatures_c[concentrator]));
    self->raw.cwt_valid_mask[concentrator] = bank->valid_channels_mask;
    self->raw.cwt_open_probe_mask[concentrator] = bank->open_probe_mask;
    self->raw.cwt_comm_ok[concentrator] = bank->is_communication_valid;

    return 0;
}

static int sensor_zephyr_set_channel_config(void *impl,
                                            physical_channel_id_t ch,
                                            const sensor_channel_cfg_t *cfg)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL || cfg == NULL || !physical_channel_is_valid(ch)) {
        return -EINVAL;
    }

    self->channel_cfg[ch] = *cfg;

    if ((size_t)ch + 1U > self->channel_cfg_count) {
        self->channel_cfg_count = (size_t)ch + 1U;
    }

    return 0;
}

static int sensor_zephyr_get_channel_config(void *impl,
                                            physical_channel_id_t ch,
                                            sensor_channel_cfg_t *out_cfg)
{
    adapter_sensor_zephyr_t *self = (adapter_sensor_zephyr_t *)impl;

    if (self == NULL || out_cfg == NULL || !physical_channel_is_valid(ch)) {
        return -EINVAL;
    }

    *out_cfg = self->channel_cfg[ch];
    return 0;
}

static const i_sensor_matrix_api_t k_adapter_api = {
    .get_frame = sensor_zephyr_get_frame,
    .get_raw_readings = sensor_zephyr_get_raw_readings,
    .set_channel_config = sensor_zephyr_set_channel_config,
    .get_channel_config = sensor_zephyr_get_channel_config,
    .update = sensor_zephyr_update,
    .submit_cwt_bank = sensor_zephyr_submit_cwt_bank,
};

/*============================================================================*
 * PUBLIC CONSTRUCTOR / PORT BINDER
 *============================================================================*/

int adapter_sensor_zephyr_init(adapter_sensor_zephyr_t *self,
                               const struct device *adc_dma_dev,
                               uint32_t scan_interval_ms)
{
    if (self == NULL || adc_dma_dev == NULL || !device_is_ready(adc_dma_dev)) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));

    self->adc_dma_dev = adc_dma_dev;
    self->adc_api = (const struct adc_dma_driver_api *)adc_dma_dev->api;
    self->scan_interval_ms = (scan_interval_ms > 0U) ? scan_interval_ms
                                                     : ADC_DMA_DEFAULT_SCAN_INTERVAL_MS;
    self->frame_valid = false;

    LOG_INF("sensor matrix adapter initialised (scan interval %u ms)",
            self->scan_interval_ms);

    return 0;
}

int adapter_sensor_zephyr_port(adapter_sensor_zephyr_t *self, i_sensor_matrix_t *out)
{
    if (self == NULL || out == NULL) {
        return -EINVAL;
    }

    out->api = &k_adapter_api;
    out->impl = self;

    return 0;
}
