/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Storage Active Object Coordinator Implementation.
 */

#include "application/active_objects/ao_storage_sd.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_storage_sd, CONFIG_LOG_DEFAULT_LEVEL);

static void publish_status(ao_storage_sd_t *self)
{
    sd_card_status_msg_t msg = {
        .status = sd_lifecycle_sm_get_status(&self->fsm),
        .card_present = sd_lifecycle_sm_is_card_present(&self->fsm),
        .is_mounted = sd_lifecycle_sm_is_ready(&self->fsm),
        .total_space_mb = (uint32_t)(self->volume_info.total_bytes / (1024ULL * 1024ULL)),
        .free_space_mb = (uint32_t)(self->volume_info.free_bytes / (1024ULL * 1024ULL)),
        .timestamp_ms = (uint32_t)k_uptime_get_32(),
    };
    (void)zbus_chan_pub(&chan_storage_sd_status, &msg, K_MSEC(50));
}

static void provision_directories(ao_storage_sd_t *self)
{
    char path[64];
    const char *dirs[] = {"history", "logs", "www"};

    for (size_t i = 0; i < ARRAY_SIZE(dirs); i++) {
        snprintf(path, sizeof(path), "%s/%s", self->mount_point, dirs[i]);
        storage_fs_stat_t st;
        int rc = i_storage_fs_stat(&self->fs, path, &st);
        if (rc != 0) {
            rc = i_storage_fs_mkdir(&self->fs, path);
            if (rc == 0) {
                LOG_INF("Created directory: %s", path);
            }
        }
    }
}

static void handle_fsm_action(ao_storage_sd_t *self, sd_lifecycle_action_t action)
{
    switch (action) {
    case SD_LIFECYCLE_ACTION_MOUNT: {
        LOG_INF("Mounting MicroSD on %s...", self->mount_point);
        int rc = i_storage_fs_mount(&self->fs, self->mount_point);
        if (rc == 0) {
            sd_lifecycle_sm_process(&self->fsm, SD_LIFECYCLE_EVT_MOUNT_SUCCESS, 0);
            provision_directories(self);
            (void)i_storage_fs_statvfs(&self->fs, self->mount_point, &self->volume_info);
            publish_status(self);
            LOG_INF("MicroSD mounted (%llu MiB free / %llu MiB total)",
                    self->volume_info.free_bytes / (1024ULL * 1024ULL),
                    self->volume_info.total_bytes / (1024ULL * 1024ULL));
        } else {
            LOG_ERR("MicroSD mount failed: %d", rc);
            sd_lifecycle_sm_process(&self->fsm, SD_LIFECYCLE_EVT_MOUNT_FAILED, rc);
            publish_status(self);
        }
        break;
    }

    case SD_LIFECYCLE_ACTION_UNMOUNT: {
        LOG_INF("Unmounting MicroSD from %s...", self->mount_point);
        (void)i_storage_fs_unmount(&self->fs, self->mount_point);
        sd_lifecycle_sm_process(&self->fsm, SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED, 0);
        memset(&self->volume_info, 0, sizeof(self->volume_info));
        publish_status(self);
        LOG_INF("MicroSD unmounted successfully");
        break;
    }

    case SD_LIFECYCLE_ACTION_PUBLISH_STATUS:
        publish_status(self);
        break;

    case SD_LIFECYCLE_ACTION_START_DEBOUNCE:
    case SD_LIFECYCLE_ACTION_NONE:
    default:
        break;
    }
}

static void ao_storage_sd_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    ao_storage_sd_t *self = (ao_storage_sd_t *)p1;

    LOG_INF("ao_storage_sd thread started");

    while (self->is_running) {
        ao_storage_sd_event_t evt = {0};
        int rc = k_msgq_get(self->queue, &evt, K_MSEC(AO_STORAGE_SD_POLL_INTERVAL_MS));

        if (!self->is_running) {
            break;
        }

        k_mutex_lock(&self->lock, K_FOREVER);

        if (rc == 0) {
            switch (evt.type) {
            case AO_STORAGE_SD_EVT_STOP:
                self->is_running = false;
                k_mutex_unlock(&self->lock);
                goto exit_thread;

            case AO_STORAGE_SD_EVT_MOUNT: {
                sd_lifecycle_action_t act = sd_lifecycle_sm_process(&self->fsm,
                                                                   SD_LIFECYCLE_EVT_MANUAL_MOUNT,
                                                                   0);
                handle_fsm_action(self, act);
                break;
            }

            case AO_STORAGE_SD_EVT_EJECT: {
                sd_lifecycle_action_t act = sd_lifecycle_sm_process(&self->fsm,
                                                                   SD_LIFECYCLE_EVT_EJECT_REQUESTED,
                                                                   0);
                handle_fsm_action(self, act);
                break;
            }

            default:
                break;
            }
        }

        /* Periodic card presence poll and debounce ticking */
        bool physical_present = i_storage_fs_is_card_present(&self->fs);
        bool sm_present = sd_lifecycle_sm_is_card_present(&self->fsm);

        if (physical_present != sm_present) {
            sd_lifecycle_event_type_t ev = physical_present ?
                SD_LIFECYCLE_EVT_CARD_DETECTED : SD_LIFECYCLE_EVT_CARD_REMOVED;
            sd_lifecycle_action_t act = sd_lifecycle_sm_process(&self->fsm, ev, 0);
            handle_fsm_action(self, act);
        } else if (self->fsm.is_debouncing) {
            sd_lifecycle_action_t act = sd_lifecycle_sm_process(&self->fsm,
                                                               SD_LIFECYCLE_EVT_TICK,
                                                               AO_STORAGE_SD_POLL_INTERVAL_MS);
            handle_fsm_action(self, act);
        }

        k_mutex_unlock(&self->lock);
    }

exit_thread:
    /* Cleanly unmount if still mounted before thread exits */
    k_mutex_lock(&self->lock, K_FOREVER);
    if (sd_lifecycle_sm_is_ready(&self->fsm)) {
        (void)i_storage_fs_unmount(&self->fs, self->mount_point);
        sd_lifecycle_sm_process(&self->fsm, SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED, 0);
        publish_status(self);
    }
    k_mutex_unlock(&self->lock);

    k_sem_give(&self->stopped_sem);
    LOG_INF("ao_storage_sd thread terminated cleanly");
}

int ao_storage_sd_init(ao_storage_sd_t *self,
                       const i_storage_fs_t *fs,
                       struct k_msgq *queue,
                       k_thread_stack_t *stack,
                       size_t stack_size,
                       int priority)
{
    if (self == NULL || fs == NULL || queue == NULL || stack == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->fs = *fs;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    strncpy(self->mount_point, "/SD:", sizeof(self->mount_point) - 1);

    k_mutex_init(&self->lock);
    k_sem_init(&self->stopped_sem, 0, 1);
    sd_lifecycle_sm_init(&self->fsm);

    k_thread_create(&self->thread,
                    self->stack,
                    self->stack_size,
                    ao_storage_sd_thread_entry,
                    self, NULL, NULL,
                    priority,
                    0,
                    K_FOREVER);

    k_thread_name_set(&self->thread, "ao_storage_sd");

    self->is_initialized = true;
    return 0;
}

int ao_storage_sd_start(ao_storage_sd_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return 0;
    }

    self->is_running = true;
    k_thread_start(&self->thread);

    ao_storage_sd_event_t evt = {.type = AO_STORAGE_SD_EVT_START};
    return k_msgq_put(self->queue, &evt, K_MSEC(100));
}

int ao_storage_sd_stop(ao_storage_sd_t *self)
{
    if (self == NULL || !self->is_initialized || !self->is_running) {
        return 0;
    }

    ao_storage_sd_event_t evt = {.type = AO_STORAGE_SD_EVT_STOP};
    int rc = k_msgq_put(self->queue, &evt, K_MSEC(100));
    if (rc != 0) {
        self->is_running = false;
    }

    (void)k_sem_take(&self->stopped_sem, K_MSEC(2000));
    return 0;
}

int ao_storage_sd_mount(ao_storage_sd_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    ao_storage_sd_event_t evt = {.type = AO_STORAGE_SD_EVT_MOUNT};
    return k_msgq_put(self->queue, &evt, K_MSEC(100));
}

int ao_storage_sd_eject(ao_storage_sd_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    ao_storage_sd_event_t evt = {.type = AO_STORAGE_SD_EVT_EJECT};
    return k_msgq_put(self->queue, &evt, K_MSEC(100));
}

int ao_storage_sd_get_status(ao_storage_sd_t *self,
                             storage_fs_card_status_t *out_status,
                             storage_fs_volume_info_t *out_vol)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    if (out_status != NULL) {
        *out_status = sd_lifecycle_sm_get_status(&self->fsm);
    }

    if (out_vol != NULL) {
        if (sd_lifecycle_sm_is_ready(&self->fsm)) {
            (void)i_storage_fs_statvfs(&self->fs, self->mount_point, &self->volume_info);
            *out_vol = self->volume_info;
        } else {
            memset(out_vol, 0, sizeof(*out_vol));
        }
    }

    k_mutex_unlock(&self->lock);
    return 0;
}
