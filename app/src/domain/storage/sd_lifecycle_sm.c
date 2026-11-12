/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Lifecycle Finite State Machine Implementation.
 * Pure C99/C11.
 */

#include "domain/storage/sd_lifecycle_sm.h"
#include <string.h>

void sd_lifecycle_sm_init(sd_lifecycle_sm_t *sm)
{
    if (sm == NULL) {
        return;
    }

    memset(sm, 0, sizeof(*sm));
    sm->status = STORAGE_FS_CARD_NOT_INSERTED;
    sm->debounce_target_ms = SD_LIFECYCLE_DEFAULT_DEBOUNCE_MS;
}

sd_lifecycle_action_t sd_lifecycle_sm_process(sd_lifecycle_sm_t *sm,
                                              sd_lifecycle_event_type_t evt,
                                              int32_t param)
{
    if (sm == NULL) {
        return SD_LIFECYCLE_ACTION_NONE;
    }

    switch (evt) {
    case SD_LIFECYCLE_EVT_CARD_DETECTED:
        sm->card_present = true;
        if (!sm->is_mounted) {
            sm->status = STORAGE_FS_CARD_INSERTED_UNMOUNTED;
            sm->is_debouncing = true;
            sm->debounce_elapsed_ms = 0;
            return SD_LIFECYCLE_ACTION_START_DEBOUNCE;
        }
        return SD_LIFECYCLE_ACTION_NONE;

    case SD_LIFECYCLE_EVT_CARD_REMOVED:
        sm->card_present = false;
        sm->is_debouncing = false;
        sm->debounce_elapsed_ms = 0;

        if (sm->is_mounted) {
            sm->status = STORAGE_FS_CARD_EJECT_REQUESTED;
            return SD_LIFECYCLE_ACTION_UNMOUNT;
        }

        sm->status = STORAGE_FS_CARD_NOT_INSERTED;
        return SD_LIFECYCLE_ACTION_PUBLISH_STATUS;

    case SD_LIFECYCLE_EVT_TICK:
        if (sm->is_debouncing) {
            uint32_t delta = (param > 0) ? (uint32_t)param : 0U;
            sm->debounce_elapsed_ms += delta;

            if (sm->debounce_elapsed_ms >= sm->debounce_target_ms) {
                sm->is_debouncing = false;
                if (sm->card_present && !sm->is_mounted) {
                    return SD_LIFECYCLE_ACTION_MOUNT;
                }
            }
        }
        return SD_LIFECYCLE_ACTION_NONE;

    case SD_LIFECYCLE_EVT_MOUNT_SUCCESS:
        sm->is_mounted = true;
        sm->is_debouncing = false;
        sm->status = STORAGE_FS_CARD_MOUNTED;
        sm->last_error = 0;
        return SD_LIFECYCLE_ACTION_PUBLISH_STATUS;

    case SD_LIFECYCLE_EVT_MOUNT_FAILED:
        sm->is_mounted = false;
        sm->is_debouncing = false;
        sm->status = STORAGE_FS_CARD_ERROR;
        sm->last_error = (int)param;
        return SD_LIFECYCLE_ACTION_PUBLISH_STATUS;

    case SD_LIFECYCLE_EVT_EJECT_REQUESTED:
        if (sm->is_mounted) {
            sm->status = STORAGE_FS_CARD_EJECT_REQUESTED;
            return SD_LIFECYCLE_ACTION_UNMOUNT;
        }
        return SD_LIFECYCLE_ACTION_NONE;

    case SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED:
        sm->is_mounted = false;
        if (sm->card_present) {
            sm->status = STORAGE_FS_CARD_INSERTED_UNMOUNTED;
        } else {
            sm->status = STORAGE_FS_CARD_NOT_INSERTED;
        }
        return SD_LIFECYCLE_ACTION_PUBLISH_STATUS;

    case SD_LIFECYCLE_EVT_MANUAL_MOUNT:
        if (sm->card_present && !sm->is_mounted) {
            sm->is_debouncing = false;
            return SD_LIFECYCLE_ACTION_MOUNT;
        }
        return SD_LIFECYCLE_ACTION_NONE;

    default:
        return SD_LIFECYCLE_ACTION_NONE;
    }
}

storage_fs_card_status_t sd_lifecycle_sm_get_status(const sd_lifecycle_sm_t *sm)
{
    return (sm != NULL) ? sm->status : STORAGE_FS_CARD_UNKNOWN;
}

bool sd_lifecycle_sm_is_ready(const sd_lifecycle_sm_t *sm)
{
    return (sm != NULL && sm->status == STORAGE_FS_CARD_MOUNTED && sm->is_mounted);
}

bool sd_lifecycle_sm_is_card_present(const sd_lifecycle_sm_t *sm)
{
    return (sm != NULL && sm->card_present);
}
