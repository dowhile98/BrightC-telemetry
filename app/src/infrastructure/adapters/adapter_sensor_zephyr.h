/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infrastructure adapter: Sensor Matrix Ingestion over Zephyr ADC DMA.
 *
 * Acquires raw ADC samples through the multi-ADC DMA engine and maps them
 * through the pure domain `sensor_matrix_map()` into a thermal matrix frame,
 * satisfying the i_sensor_matrix_api_t port contract.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SENSOR_ZEPHYR_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SENSOR_ZEPHYR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#include "common/sensor_types.h"
#include "interfaces/i_sensor_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The driver API table is only referenced through a pointer, so the concrete
 * driver header stays in the implementation file. This keeps every consumer of
 * this header (notably the DI container and the unit-test suites) free of
 * hardware driver includes.
 */
struct adc_dma_driver_api;

/**
 * @brief One sensor matrix ingestion instance backed by the ADC DMA engine.
 *
 * Owns the channel configuration table, the raw readings bank and the mapped
 * thermal frame. All storage is static; the instance must not be copied after
 * initialisation.
 */
typedef struct {
    const struct device          *adc_dma_dev;  /**< ADC DMA engine device */
    const struct adc_dma_driver_api *adc_api;   /**< Driver API table */

    sensor_channel_cfg_t channel_cfg[SENSOR_PHYS_CH_MAX]; /**< Channel mapping table */
    size_t               channel_cfg_count;     /**< Highest configured index + 1 */

    raw_sensor_readings_t  raw;                 /**< Latest physical readings bank */
    thermal_matrix_frame_t frame;               /**< Latest mapped semantic frame */
    bool                   frame_valid;         /**< True after one successful map */

    uint32_t scan_interval_ms;                  /**< Recommended acquisition period */
    uint8_t  consecutive_failures;              /**< Consecutive acquisition failure counter */
} adapter_sensor_zephyr_t;

/**
 * @brief Initialise the sensor matrix adapter.
 *
 * @param[out] self             Adapter instance (static storage, never copied).
 * @param[in]  adc_dma_dev      ADC DMA engine device (provided by the composition root).
 * @param[in]  scan_interval_ms Recommended period between acquisition cycles.
 *
 * @return 0 on success, -EINVAL on bad arguments or unready device.
 */
int adapter_sensor_zephyr_init(adapter_sensor_zephyr_t *self,
                               const struct device *adc_dma_dev,
                               uint32_t scan_interval_ms);

/**
 * @brief Expose the adapter through the sensor matrix port.
 *
 * @param[in]  self Adapter instance.
 * @param[out] out  Handle to fill.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int adapter_sensor_zephyr_port(adapter_sensor_zephyr_t *self, i_sensor_matrix_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_SENSOR_ZEPHYR_H_ */
