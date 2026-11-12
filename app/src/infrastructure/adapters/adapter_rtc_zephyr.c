/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_rtc_zephyr.c
 * @brief Infrastructure adapter binding Zephyr <zephyr/drivers/rtc.h> to abstract i_rtc_t port.
 */

#include "infrastructure/adapters/adapter_rtc_zephyr.h"
#include "domain/time/rtc_time_calc.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_SOC_FAMILY_STM32)
#include <stm32_ll_pwr.h>
#include <stm32_ll_rtc.h>
#include <stm32_ll_rcc.h>
#endif

LOG_MODULE_REGISTER(adapter_rtc_zephyr, LOG_LEVEL_INF);

/* Helper to read BKP0R magic word on STM32 hardware */
static inline uint32_t read_hardware_bkp0(void)
{
#if defined(CONFIG_SOC_FAMILY_STM32) && defined(TAMP) && defined(LL_RTC_BKP_DR0)
    return LL_RTC_BKP_GetRegister(TAMP, LL_RTC_BKP_DR0);
#else
    return 0;
#endif
}

/* Helper to write BKP0R magic word on STM32 hardware */
static inline void write_hardware_bkp0(uint32_t val)
{
#if defined(CONFIG_SOC_FAMILY_STM32) && defined(TAMP) && defined(LL_RTC_BKP_DR0)
    LL_PWR_EnableBkUpAccess();
    LL_RTC_BKP_SetRegister(TAMP, LL_RTC_BKP_DR0, val);
#else
    (void)val;
#endif
}

/* Helper to check LSE oscillator running status on STM32 hardware */
static inline bool check_hardware_oscillator_running(void)
{
#if defined(CONFIG_SOC_FAMILY_STM32) && defined(RCC_BDCR_LSERDY)
    return (LL_RCC_LSE_IsReady() == 1);
#else
    return true;
#endif
}

static int adapter_rtc_impl_init(void *impl)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }

    if (self->dev == NULL) {
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(rtc))
        self->dev = DEVICE_DT_GET(DT_NODELABEL(rtc));
#else
        self->dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(rtc));
#endif
    }

    if (self->dev == NULL || !device_is_ready(self->dev)) {
        LOG_WRN("Zephyr RTC device is not ready or NULL");
        self->sync_status.sync_state = RTC_SYNC_STATE_HARDWARE_FAULT;
        self->sync_status.oscillator_running = false;
        return -ENODEV;
    }

    self->sync_status.oscillator_running = check_hardware_oscillator_running();

    uint32_t bkp0 = read_hardware_bkp0();
    if (bkp0 == RTC_BKP_MAGIC_WORD || self->magic_word == RTC_BKP_MAGIC_WORD) {
        LOG_INF("RTC restored from VBAT domain (BKP0R=0x%08X). Time retained.", bkp0);
        self->sync_status.sync_state = RTC_SYNC_STATE_BATTERY_RETAINED;
        self->sync_status.last_sync_source = RTC_TIME_SOURCE_VBAT_BACKUP;
        self->sync_status.battery_ok = true;
        self->magic_word = RTC_BKP_MAGIC_WORD;
    } else {
        LOG_WRN("Cold boot / uninitialized RTC (BKP0R=0x%08X != 0x%08X)", bkp0, RTC_BKP_MAGIC_WORD);
        self->sync_status.sync_state = RTC_SYNC_STATE_UNINITIALIZED;
        self->sync_status.last_sync_source = RTC_TIME_SOURCE_NONE;
        self->sync_status.battery_ok = false;
    }

    self->is_initialized = true;
    return 0;
}

static int adapter_rtc_impl_get_time(void *impl, rtc_date_time_t *out_dt)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL || out_dt == NULL) {
        return -EINVAL;
    }
    if (self->dev == NULL || !device_is_ready(self->dev)) {
        return -ENODEV;
    }

    struct rtc_time tm = {0};
    int err = rtc_get_time(self->dev, &tm);
    if (err != 0) {
        LOG_ERR("rtc_get_time() failed: %d", err);
        return err;
    }

    out_dt->year = (uint16_t)(tm.tm_year + 1900);
    out_dt->month = (uint8_t)(tm.tm_mon + 1);
    out_dt->day = (uint8_t)tm.tm_mday;
    out_dt->hours = (uint8_t)tm.tm_hour;
    out_dt->minutes = (uint8_t)tm.tm_min;
    out_dt->seconds = (uint8_t)tm.tm_sec;
    out_dt->milliseconds = (uint16_t)(tm.tm_nsec / 1000000);

    if (tm.tm_wday >= 0 && tm.tm_wday <= 6) {
        out_dt->day_of_week = (tm.tm_wday == 0) ? 7 : (uint8_t)tm.tm_wday;
    } else {
        out_dt->day_of_week = rtc_calc_day_of_week(out_dt->year, out_dt->month, out_dt->day);
    }

    return 0;
}

static int adapter_rtc_impl_set_time(void *impl, const rtc_date_time_t *dt)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL || dt == NULL) {
        return -EINVAL;
    }
    if (!rtc_calc_is_valid_datetime(dt)) {
        return -EINVAL;
    }
    if (self->dev == NULL || !device_is_ready(self->dev)) {
        return -ENODEV;
    }

    struct rtc_time tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)dt->year - 1900;
    tm.tm_mon = (int)dt->month - 1;
    tm.tm_mday = (int)dt->day;
    tm.tm_hour = (int)dt->hours;
    tm.tm_min = (int)dt->minutes;
    tm.tm_sec = (int)dt->seconds;
    tm.tm_nsec = (int)dt->milliseconds * 1000000;
    tm.tm_wday = (dt->day_of_week == 7) ? 0 : (int)dt->day_of_week;
    tm.tm_yday = -1;
    tm.tm_isdst = -1;

    int err = rtc_set_time(self->dev, &tm);
    if (err != 0) {
        LOG_ERR("rtc_set_time() failed: %d", err);
        return err;
    }

    /* Record retention magic word in hardware backup registers and local state */
    write_hardware_bkp0(RTC_BKP_MAGIC_WORD);
    self->magic_word = RTC_BKP_MAGIC_WORD;
    self->sync_status.battery_ok = true;

    return 0;
}

static int adapter_rtc_impl_get_epoch(void *impl, int64_t *out_epoch_s, uint32_t *out_ms)
{
    if (out_epoch_s == NULL) {
        return -EINVAL;
    }

    rtc_date_time_t dt;
    int err = adapter_rtc_impl_get_time(impl, &dt);
    if (err != 0) {
        return err;
    }

    err = rtc_calc_datetime_to_epoch(&dt, out_epoch_s);
    if (err != 0) {
        return err;
    }

    if (out_ms != NULL) {
        *out_ms = dt.milliseconds;
    }

    return 0;
}

static int adapter_rtc_impl_set_epoch(void *impl, int64_t epoch_s, uint32_t ms)
{
    if (epoch_s < 0 || ms > 999) {
        return -EINVAL;
    }

    rtc_date_time_t dt;
    int err = rtc_calc_epoch_to_datetime(epoch_s, (uint16_t)ms, &dt);
    if (err != 0) {
        return err;
    }

    return adapter_rtc_impl_set_time(impl, &dt);
}

static int adapter_rtc_impl_get_sync_status(void *impl, rtc_sync_status_t *out_status)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL || out_status == NULL) {
        return -EINVAL;
    }

    self->sync_status.oscillator_running = check_hardware_oscillator_running();
    *out_status = self->sync_status;
    return 0;
}

static int adapter_rtc_impl_set_sync_status(void *impl, const rtc_sync_status_t *status)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL || status == NULL) {
        return -EINVAL;
    }

    self->sync_status = *status;
    if (status->sync_state == RTC_SYNC_STATE_SYNCHRONIZED ||
        status->sync_state == RTC_SYNC_STATE_BATTERY_RETAINED) {
        write_hardware_bkp0(RTC_BKP_MAGIC_WORD);
        self->magic_word = RTC_BKP_MAGIC_WORD;
    }

    return 0;
}

static int adapter_rtc_impl_set_calibration(void *impl, int32_t ppb_offset)
{
    adapter_rtc_zephyr_t *self = (adapter_rtc_zephyr_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }

    self->calibration_ppb = ppb_offset;
    self->sync_status.estimated_drift_ppb = ppb_offset;

#if defined(CONFIG_RTC_CALIBRATION)
    if (self->dev != NULL && device_is_ready(self->dev)) {
        return rtc_set_calibration(self->dev, ppb_offset);
    }
#endif

    return 0;
}

static const i_rtc_api_t s_adapter_rtc_api = {
    .init            = adapter_rtc_impl_init,
    .get_time        = adapter_rtc_impl_get_time,
    .set_time        = adapter_rtc_impl_set_time,
    .get_epoch       = adapter_rtc_impl_get_epoch,
    .set_epoch       = adapter_rtc_impl_set_epoch,
    .get_sync_status = adapter_rtc_impl_get_sync_status,
    .set_sync_status = adapter_rtc_impl_set_sync_status,
    .set_calibration = adapter_rtc_impl_set_calibration,
};

int adapter_rtc_zephyr_init(adapter_rtc_zephyr_t *self, const struct device *dev)
{
    if (self == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->dev = dev;

    return adapter_rtc_impl_init(self);
}

i_rtc_t adapter_rtc_zephyr_bind(adapter_rtc_zephyr_t *self)
{
    i_rtc_t port;
    port.api = &s_adapter_rtc_api;
    port.impl = self;
    return port;
}
