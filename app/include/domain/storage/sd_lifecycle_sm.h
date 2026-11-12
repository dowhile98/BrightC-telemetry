/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Lifecycle Finite State Machine (Domain Layer).
 * Handles card detection, contact debounce timing (150 ms), mount/unmount orchestration,
 * eject requests, and surprise removal protection.
 *
 * Pure C99/C11 - zero Zephyr headers, zero RTOS dependencies.
 */

#ifndef APP_DOMAIN_STORAGE_SD_LIFECYCLE_SM_H_
#define APP_DOMAIN_STORAGE_SD_LIFECYCLE_SM_H_

#include <stdbool.h>
#include <stdint.h>

#include "common/storage_fs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_LIFECYCLE_DEFAULT_DEBOUNCE_MS 150U

typedef enum {
    SD_LIFECYCLE_EVT_NONE = 0,
    SD_LIFECYCLE_EVT_CARD_DETECTED,      /**< Card detect switch closed (card inserted) */
    SD_LIFECYCLE_EVT_CARD_REMOVED,       /**< Card detect switch opened (card removed) */
    SD_LIFECYCLE_EVT_TICK,               /**< Periodic timer step */
    SD_LIFECYCLE_EVT_MOUNT_SUCCESS,      /**< Filesystem mount succeeded */
    SD_LIFECYCLE_EVT_MOUNT_FAILED,       /**< Filesystem mount failed */
    SD_LIFECYCLE_EVT_EJECT_REQUESTED,    /**< User/Software eject request */
    SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED,  /**< Filesystem unmount succeeded */
    SD_LIFECYCLE_EVT_MANUAL_MOUNT,       /**< User/Software manual mount request */
} sd_lifecycle_event_type_t;

typedef enum {
    SD_LIFECYCLE_ACTION_NONE = 0,
    SD_LIFECYCLE_ACTION_START_DEBOUNCE,  /**< Start debounce timer */
    SD_LIFECYCLE_ACTION_MOUNT,           /**< Trigger filesystem mount */
    SD_LIFECYCLE_ACTION_UNMOUNT,         /**< Trigger filesystem unmount */
    SD_LIFECYCLE_ACTION_PUBLISH_STATUS,  /**< Publish status notification to Zbus */
} sd_lifecycle_action_t;

typedef struct {
    storage_fs_card_status_t status;     /**< High-level status */
    bool                     card_present;
    bool                     is_mounted;
    bool                     is_debouncing;
    uint32_t                 debounce_elapsed_ms;
    uint32_t                 debounce_target_ms;
    int                      last_error;
} sd_lifecycle_sm_t;

/**
 * @brief Initialize the SD card lifecycle state machine.
 *
 * @param[out] sm State machine context.
 */
void sd_lifecycle_sm_init(sd_lifecycle_sm_t *sm);

/**
 * @brief Process an incoming event through the state machine.
 *
 * @param[in,out] sm       State machine context.
 * @param[in]     evt      Event type.
 * @param[in]     param    Optional event parameter (delta_ms for TICK, error_code for MOUNT_FAILED).
 * @return Recommended action for the Active Object coordinator.
 */
sd_lifecycle_action_t sd_lifecycle_sm_process(sd_lifecycle_sm_t *sm,
                                              sd_lifecycle_event_type_t evt,
                                              int32_t param);

/**
 * @brief Get current card lifecycle status.
 */
storage_fs_card_status_t sd_lifecycle_sm_get_status(const sd_lifecycle_sm_t *sm);

/**
 * @brief Check if filesystem is fully mounted and ready for I/O.
 */
bool sd_lifecycle_sm_is_ready(const sd_lifecycle_sm_t *sm);

/**
 * @brief Check if physical card is currently detected in slot.
 */
bool sd_lifecycle_sm_is_card_present(const sd_lifecycle_sm_t *sm);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_STORAGE_SD_LIFECYCLE_SM_H_ */
