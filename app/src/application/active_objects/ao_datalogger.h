/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_datalogger.h
 * @brief Unified Historical Telemetry & Event-Driven Datalogger Active Object.
 *
 * Adheres strictly to the canonical 5-phase Active Object lifecycle:
 * init -> start -> stop -> wait_stopped -> deinit.
 * Zero static mutable state; all state resides inside ao_datalogger_t.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_DATALOGGER_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_DATALOGGER_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "common/datalogger_types.h"
#include "interfaces/i_bkpsram.h"
#include "interfaces/i_storage_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AO_DATALOGGER_STACK_SIZE        4096U
#define AO_DATALOGGER_PRIORITY          9
#define AO_DATALOGGER_QUEUE_DEPTH       64U
#define AO_DATALOGGER_SECTOR_SIZE       512U
#define AO_DATALOGGER_FLUSH_INTERVAL_MS 5000U
#define AO_DATALOGGER_LOW_WATERMARK_MB  100U
#define AO_DATALOGGER_MAX_BOOT_FILES    5U

typedef struct {
    uint32_t events_received;
    uint32_t events_written;
    uint32_t events_dropped;
    uint32_t flushes_performed;
    uint32_t sectors_written;
    uint32_t bkpsram_recovered;
} ao_datalogger_stats_t;

typedef struct {
    i_storage_fs_t          fs;
    i_bkpsram_t             bkp;
    struct k_msgq          *queue;
    struct k_thread         thread;
    k_tid_t                 tid;
    k_thread_stack_t       *stack;
    size_t                  stack_size;
    int                     priority;
    struct k_sem            stopped_sem;
    struct k_mutex          lock;

    /* Lifecycle flags */
    volatile bool           is_running;
    bool                    is_initialized;
    bool                    is_sd_mounted;
    bool                    is_time_synced;
    char                    mount_point[32];
    uint32_t                boot_count;

    /* Dedicated 512-byte RAM stream buffers (sector-aligned) */
    uint8_t                 alarms_buf[AO_DATALOGGER_SECTOR_SIZE];
    size_t                  alarms_buf_len;
    uint8_t                 system_buf[AO_DATALOGGER_SECTOR_SIZE];
    size_t                  system_buf_len;
    uint8_t                 boot_buf[AO_DATALOGGER_SECTOR_SIZE];
    size_t                  boot_buf_len;

    /* Active file handles */
    void                   *alarms_file;
    void                   *system_file;
    void                   *boot_file;
    char                    current_date[16];   /* "YYYY-MM-DD" */

    /* Timing & Metrics */
    int64_t                 last_flush_uptime_ms;
    ao_datalogger_stats_t   stats;
} ao_datalogger_t;

/**
 * @brief Initialize the Unified Datalogger Active Object.
 *
 * @param[in,out] self       Active Object context.
 * @param[in]     fs         Filesystem abstract port handle.
 * @param[in]     bkp        Backup SRAM abstract port handle.
 * @param[in]     queue      Allocated message queue for ingress events.
 * @param[in]     stack      Allocated thread stack.
 * @param[in]     stack_size Thread stack size in bytes.
 * @param[in]     priority   Thread execution priority.
 * @return 0 on success, negative errno on failure.
 */
int ao_datalogger_init(ao_datalogger_t *self,
                      const i_storage_fs_t *fs,
                      const i_bkpsram_t *bkp,
                      struct k_msgq *queue,
                      k_thread_stack_t *stack,
                      size_t stack_size,
                      int priority);

/**
 * @brief Start the Datalogger Active Object thread.
 */
int ao_datalogger_start(ao_datalogger_t *self);

/**
 * @brief Stop the Datalogger Active Object gracefully.
 */
int ao_datalogger_stop(ao_datalogger_t *self);

/**
 * @brief Wait until the Datalogger Active Object thread terminates.
 */
int ao_datalogger_wait_stopped(ao_datalogger_t *self, k_timeout_t timeout);

/**
 * @brief De-initialize the Datalogger Active Object and free resources.
 */
int ao_datalogger_deinit(ao_datalogger_t *self);

/**
 * @brief Post an event envelope to the datalogger queue (non-blocking).
 *
 * Drops lowest priority items if queue is full.
 */
int ao_datalogger_post_event(ao_datalogger_t *self, const datalogger_event_envelope_t *evt);

/**
 * @brief Query current operational metrics and diagnostics.
 */
int ao_datalogger_get_stats(ao_datalogger_t *self, ao_datalogger_stats_t *out_stats);

/**
 * @brief Set the current calendar date string (e.g. "2026-11-14") and transition to time-synced mode.
 */
int ao_datalogger_set_date(ao_datalogger_t *self, const char *date_str);

/**
 * @brief Request an immediate flush of all active stream buffers to storage.
 */
int ao_datalogger_flush(ao_datalogger_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_DATALOGGER_H_ */
