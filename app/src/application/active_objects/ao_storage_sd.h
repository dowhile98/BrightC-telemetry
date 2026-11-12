/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Storage Active Object Coordinator.
 * Manages card hot-plug lifecycle, contact debouncing (150 ms), automatic mount/unmount,
 * directory structure provisioning (/SD:/history, /SD:/logs, /SD:/www),
 * and Zbus status notification.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_SD_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_SD_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "common/storage_fs_types.h"
#include "interfaces/i_storage_fs.h"
#include "domain/storage/sd_lifecycle_sm.h"
#include "application/zbus/storage_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AO_STORAGE_SD_STACK_SIZE 3072
#define AO_STORAGE_SD_PRIORITY 8
#define AO_STORAGE_SD_QUEUE_DEPTH 8
#define AO_STORAGE_SD_POLL_INTERVAL_MS 200U

typedef enum {
    AO_STORAGE_SD_EVT_NONE = 0,
    AO_STORAGE_SD_EVT_START,
    AO_STORAGE_SD_EVT_STOP,
    AO_STORAGE_SD_EVT_MOUNT,
    AO_STORAGE_SD_EVT_EJECT,
} ao_storage_sd_event_type_t;

typedef struct {
    ao_storage_sd_event_type_t type;
} ao_storage_sd_event_t;

typedef struct {
    i_storage_fs_t           fs;
    struct k_msgq           *queue;
    struct k_thread          thread;
    k_thread_stack_t        *stack;
    size_t                   stack_size;
    struct k_sem             stopped_sem;
    struct k_mutex           lock;
    sd_lifecycle_sm_t        fsm;
    storage_fs_volume_info_t volume_info;
    char                     mount_point[32];
    volatile bool            is_running;
    bool                     is_initialized;
} ao_storage_sd_t;

/**
 * @brief Initialize the MicroSD storage Active Object coordinator.
 *
 * @param[in,out] self       Active Object instance.
 * @param[in]     fs         Storage filesystem port handle.
 * @param[in]     queue      Allocated message queue.
 * @param[in]     stack      Allocated thread stack.
 * @param[in]     stack_size Size of thread stack in bytes.
 * @param[in]     priority   Thread priority.
 * @return 0 on success, negative errno on failure.
 */
int ao_storage_sd_init(ao_storage_sd_t *self,
                       const i_storage_fs_t *fs,
                       struct k_msgq *queue,
                       k_thread_stack_t *stack,
                       size_t stack_size,
                       int priority);

/**
 * @brief Start the Active Object coordinator thread.
 */
int ao_storage_sd_start(ao_storage_sd_t *self);

/**
 * @brief Stop the Active Object coordinator thread gracefully.
 */
int ao_storage_sd_stop(ao_storage_sd_t *self);

/**
 * @brief Post a manual mount request.
 */
int ao_storage_sd_mount(ao_storage_sd_t *self);

/**
 * @brief Post a safe eject / unmount request.
 */
int ao_storage_sd_eject(ao_storage_sd_t *self);

/**
 * @brief Query current card status and volume statistics.
 *
 * @param[in]  self       Active Object instance.
 * @param[out] out_status Destination status pointer (may be NULL).
 * @param[out] out_vol    Destination volume info pointer (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int ao_storage_sd_get_status(ao_storage_sd_t *self,
                             storage_fs_card_status_t *out_status,
                             storage_fs_volume_info_t *out_vol);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_SD_H_ */
