/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file datalogger_types.h
 * @brief Pure domain types and data contracts for the Unified Event & Datalogger Subsystem.
 *
 * ISO C99 / C11 compliant. Zero Zephyr RTOS or vendor hardware dependencies.
 */

#ifndef APP_COMMON_DATALOGGER_TYPES_H_
#define APP_COMMON_DATALOGGER_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/alarm_types.h"
#include "common/hvac_types.h"
#include "common/storage_fs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Magic signature for non-volatile backup records: 'ALM_LOG_V1' */
#define ALARM_NV_RECORD_MAGIC       0xA1A83C01U

/* Status flags for alarm_nv_record_t */
#define ALARM_NV_FLAG_NONE          0x00U
#define ALARM_NV_FLAG_COMMITTED_SD  0x01U

/* Capacity of the 4 KB circular ring buffer in Backup SRAM (64 entries * 64 bytes) */
#define BKPSRAM_RING_CAPACITY       64U
#define BKPSRAM_RING_BUFFER_SIZE    (BKPSRAM_RING_CAPACITY * 64U)

/**
 * @brief Persistent non-volatile critical alarm record stored in Backup SRAM.
 *
 * Exactly 64 bytes in length (power-of-two / cache-line friendly).
 * Preserves high-resolution trip forensics across surprise power interruptions.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                 /**< 0xA1A83C01 ('ALM_LOG_V1') */
    uint32_t seq_id;                /**< Strictly monotonic global event sequence */
    uint64_t uptime_ms;             /**< Hardware uptime in milliseconds at event */
    uint32_t epoch_s;               /**< Unix epoch seconds (or 0 if unsynchronized) */
    uint16_t alarm_id;              /**< Domain alarm identifier */
    uint8_t  source_subsystem;      /**< Origin subsystem (HVAC, POWER, NETWORK) */
    uint8_t  transition_type;       /**< TRIP (1), CLEAR (2), ACK (3) */
    float    threshold_value;       /**< Engineering threshold/setpoint triggered */
    float    measured_value;        /**< Engineering value measured at transition */
    uint8_t  severity;              /**< INFO, WARNING, CRITICAL, FATAL */
    uint8_t  status_flags;          /**< Bit 0: ALARM_NV_FLAG_COMMITTED_SD */
    char     tag[26];               /**< Alphanumeric descriptor (e.g. "AC1_HP_TRIP") */
    uint8_t  reserved[2];           /**< Alignment padding to 64 bytes */
    uint16_t crc16;                 /**< CRC-16-CCITT across preceding 62 bytes */
} alarm_nv_record_t;

/* Compile-time verification of strict 64-byte packed footprint */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(alarm_nv_record_t) == 64U, "alarm_nv_record_t must be exactly 64 bytes");
#endif

/**
 * @brief Logical logging destination streams.
 */
typedef enum {
    LOG_STREAM_ALARMS = 0,          /**< Forensic Sequence of Events (SOE) CSV */
    LOG_STREAM_SYSTEM,              /**< Operational system lifecycle text log */
    LOG_STREAM_BOOT,                /**< Pre-NTP cold boot staging CSV */
    LOG_STREAM_MAX
} log_stream_t;

/**
 * @brief System event categories and notifications.
 */
typedef enum {
    SYSTEM_EVENT_BOOT = 0,
    SYSTEM_EVENT_STORAGE_MOUNT,
    SYSTEM_EVENT_STORAGE_UNMOUNT,
    SYSTEM_EVENT_STORAGE_FAULT,
    SYSTEM_EVENT_STORAGE_PRUNE,
    SYSTEM_EVENT_TIME_SYNC,
    SYSTEM_EVENT_QUEUE_OVERFLOW,
    SYSTEM_EVENT_REBOOT_REQUEST,
    SYSTEM_EVENT_NETWORK_UP,
    SYSTEM_EVENT_NETWORK_DOWN,
    SYSTEM_EVENT_MAX
} system_event_type_t;

/**
 * @brief Payload for operational system events (`chan_system_event`).
 */
typedef struct {
    system_event_type_t type;
    alarm_severity_t    severity;
    char                subsystem[16];
    char                message[64];
    uint32_t            data_u32;
    int64_t             data_i64;
    uint64_t            timestamp_ms;
} system_event_msg_t;

/**
 * @brief Event type tag for the datalogger ingress message queue.
 */
typedef enum {
    DATALOGGER_EVT_NONE = 0,
    DATALOGGER_EVT_ALARM_TRANSITION,
    DATALOGGER_EVT_HVAC_EVENT,
    DATALOGGER_EVT_SYSTEM_EVENT,
    DATALOGGER_EVT_STORAGE_STATUS,
    DATALOGGER_EVT_TIME_SYNC_TRIGGER,
    DATALOGGER_EVT_FLUSH_TRIGGER,
    DATALOGGER_EVT_STOP,
} datalogger_event_type_t;

/**
 * @brief Universal tagged union envelope for datalogger queue messages.
 */
typedef struct {
    datalogger_event_type_t type;
    uint32_t                seq_id;
    uint64_t                uptime_ms;
    union {
        alarm_transition_event_t alarm;
        hvac_event_t             hvac;
        system_event_msg_t       system;
        storage_fs_card_status_t storage_status;
    } payload;
} datalogger_event_envelope_t;

/**
 * @brief Monotonic Sequence ID generator interface.
 */
uint32_t datalogger_next_seq_id(void);
void datalogger_set_seq_id(uint32_t val);

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_DATALOGGER_TYPES_H_ */
