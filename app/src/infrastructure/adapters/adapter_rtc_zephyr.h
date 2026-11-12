/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_rtc_zephyr.h
 * @brief Infrastructure adapter binding Zephyr <zephyr/drivers/rtc.h> to abstract i_rtc_t port.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RTC_ZEPHYR_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RTC_ZEPHYR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#include "common/rtc_types.h"
#include "interfaces/i_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_BKP_MAGIC_WORD  0xA55A0002U

typedef struct {
    const struct device *dev;
    rtc_sync_status_t   sync_status;
    bool                is_initialized;
    uint32_t            magic_word;
    int32_t             calibration_ppb;
} adapter_rtc_zephyr_t;

/**
 * @brief Initialize the Zephyr hardware RTC adapter.
 *
 * Checks peripheral readiness, inspects backup domain retention magic word (0xA55A0002),
 * and verifies oscillator status.
 *
 * @param[in,out] self Adapter instance.
 * @param[in]     dev  Zephyr RTC device pointer (if NULL, defaults to DT_NODELABEL(rtc)).
 * @return 0 on success, negative errno on failure.
 */
int adapter_rtc_zephyr_init(adapter_rtc_zephyr_t *self, const struct device *dev);

/**
 * @brief Bind adapter to abstract i_rtc_t port interface handle.
 *
 * @param[in] self Adapter instance.
 * @return Abstract i_rtc_t port handle.
 */
i_rtc_t adapter_rtc_zephyr_bind(adapter_rtc_zephyr_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_RTC_ZEPHYR_H_ */
