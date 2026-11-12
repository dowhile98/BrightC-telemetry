/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, event definitions and status structures
 * for the Real-Time Clock (RTC) and System Time Subsystem on BrightC Controller V3.
 *
 * Pure C99/C11 - zero Zephyr headers, zero hardware registers.
 */

#ifndef APP_COMMON_RTC_TYPES_H_
#define APP_COMMON_RTC_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_ISO8601_STR_LEN     32  /**< "YYYY-MM-DDTHH:MM:SS.sssZ" + NUL */
#define RTC_DATE_STR_LEN        11  /**< "YYYY-MM-DD" + NUL */
#define RTC_TIME_STR_LEN        9   /**< "HH:MM:SS" + NUL */

/**
 * @brief Time source authority hierarchy.
 */
typedef enum {
    RTC_TIME_SOURCE_NONE            = 0, /**< No time source established */
    RTC_TIME_SOURCE_BUILD_DEFAULT   = 1, /**< Compile-time fallback baseline */
    RTC_TIME_SOURCE_VBAT_BACKUP     = 2, /**< Restored from battery-backed hardware RTC */
    RTC_TIME_SOURCE_MANUAL_SHELL    = 3, /**< Manually entered via Zephyr Shell */
    RTC_TIME_SOURCE_HMI_DWIN        = 4, /**< Configured via local DWIN touch panel */
    RTC_TIME_SOURCE_CLOUD_MQTT      = 5, /**< Synchronized from MQTT broker telemetry ack */
    RTC_TIME_SOURCE_SNTP            = 6, /**< Network NTP Stratum clock (highest authority) */
    RTC_TIME_SOURCE_MODBUS_TCP      = 7, /**< Synchronized via SCADA Modbus TCP write */
} rtc_time_source_t;

/**
 * @brief System time synchronization state.
 */
typedef enum {
    RTC_SYNC_STATE_UNINITIALIZED    = 0, /**< Clock has not been set; time invalid */
    RTC_SYNC_STATE_BATTERY_RETAINED = 1, /**< Clock running from VBAT, not yet network synced */
    RTC_SYNC_STATE_SYNCHRONIZED     = 2, /**< Synchronized to authoritative reference (SNTP/Cloud) */
    RTC_SYNC_STATE_DRIFTING         = 3, /**< Was synced, but refresh timeout expired without update */
    RTC_SYNC_STATE_HARDWARE_FAULT   = 4, /**< Oscillator stoppage or peripheral communication failure */
} rtc_sync_state_t;

/**
 * @brief Canonical calendar date and time structure.
 */
typedef struct {
    uint16_t year;        /**< Full 4-digit calendar year (e.g. 2026) */
    uint8_t  month;       /**< 1..12 (1 = January) */
    uint8_t  day;         /**< 1..31 */
    uint8_t  hours;       /**< 0..23 (24-hour military format) */
    uint8_t  minutes;     /**< 0..59 */
    uint8_t  seconds;     /**< 0..59 */
    uint16_t milliseconds;/**< 0..999 (subsecond fraction) */
    uint8_t  day_of_week; /**< 1..7 (1 = Monday, 7 = Sunday per ISO-8601) */
} rtc_date_time_t;

/**
 * @brief Time synchronization diagnostic status.
 */
typedef struct {
    rtc_sync_state_t  sync_state;          /**< Current synchronization state */
    rtc_time_source_t last_sync_source;    /**< Source that committed the last time adjustment */
    int64_t           last_sync_epoch_s;   /**< UTC epoch seconds when last sync occurred */
    int64_t           last_sync_uptime_ms; /**< Kernel uptime (ms) when last sync occurred */
    int32_t           estimated_drift_ppb; /**< Estimated drift in parts-per-billion */
    uint32_t          sync_count;          /**< Total successful synchronization cycles */
    bool              battery_ok;          /**< true if VBAT is within operating threshold */
    bool              oscillator_running;  /**< true if LSE 32.768 kHz is locked and stable */
} rtc_sync_status_t;

/**
 * @brief Event published over Zbus when system clock is adjusted or synced.
 */
typedef struct {
    rtc_sync_state_t  new_state;
    rtc_time_source_t source;
    int64_t           epoch_before_s;
    int64_t           epoch_after_s;
    int64_t           delta_ms;            /**< Step adjustment (positive = forward, negative = backward) */
} rtc_clock_event_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_RTC_TYPES_H_ */
