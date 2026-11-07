/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_sensor_matrix.c
 * @brief Active Object coordinator for the Sensor Matrix ingestion cycle.
 */

#include "application/active_objects/ao_sensor_matrix.h"
#include "application/zbus/sensor_channels.h"
#include "domain/services/sensor_matrix_mapper.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_sensor_matrix, LOG_LEVEL_INF);

static void ao_sensor_matrix_thread_entry(void *p1, void *p2, void *p3)
{
    ao_sensor_matrix_t *self = (ao_sensor_matrix_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!self->terminate) {
        /* 1. Blocked on outer gate until ao_sensor_matrix_start() or deinit() */
        (void)k_sem_take(&self->run_sem, K_FOREVER);
        if (self->terminate) {
            break;
        }

        LOG_INF("ao_sensor_matrix thread entered inner loop");
        self->last_scan_ms = (uint64_t)k_uptime_get();

        /* 2. Inner work loop */
        while (self->is_running) {
            if (self->queue != NULL) {
                ao_sensor_matrix_event_t evt;
                int ret = k_msgq_get(self->queue, &evt, K_MSEC(self->poll_period_ms));
                if (ret == 0 && evt.type == AO_SENSOR_MATRIX_EVENT_STOP) {
                    break;
                }
                /* AO_SENSOR_MATRIX_EVENT_UPDATE_NOW or timeout: run one cycle. */
            } else {
                k_msleep(self->poll_period_ms);
            }

            if (!self->is_running) {
                break;
            }

            uint64_t now_ms = (uint64_t)k_uptime_get();
            uint32_t elapsed_ms = (self->last_scan_ms > 0ULL && now_ms >= self->last_scan_ms)
                                      ? (uint32_t)(now_ms - self->last_scan_ms)
                                      : self->poll_period_ms;
            if (elapsed_ms == 0U) {
                elapsed_ms = self->poll_period_ms;
            }
            self->last_scan_ms = now_ms;

            (void)ao_sensor_matrix_step(self, elapsed_ms);
        }

        LOG_INF("ao_sensor_matrix thread exited inner loop");
        self->is_running = false;
        (void)k_sem_give(&self->stopped_sem);
    }

    LOG_INF("ao_sensor_matrix thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_sensor_matrix_init(ao_sensor_matrix_t *self,
                          const ao_sensor_matrix_config_t *config,
                          struct k_msgq *queue)
{
    if (self == NULL || config == NULL || config->matrix == NULL ||
        queue == NULL || config->stack == NULL || config->stack_size == 0U) {
        return -EINVAL;
    }

    if (!i_sensor_matrix_is_bound(config->matrix)) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));

    self->matrix = config->matrix;
    self->poll_period_ms = (config->poll_period_ms > 0U) ? config->poll_period_ms
                                                         : AO_SENSOR_MATRIX_PERIOD_MS;
    self->sequence = 0U;
    self->queue = queue;
    self->stack = config->stack;
    self->stack_size = config->stack_size;
    self->is_running = false;
    self->terminate = false;
    self->last_scan_ms = 0ULL;
    self->error_count = 0U;

    k_sem_init(&self->run_sem, 0, 1);
    k_sem_init(&self->stopped_sem, 0, 1);

    self->is_initialized = true;

    /* Spawn the thread once, immediately blocked on the gate. */
    (void)k_thread_create(&self->thread,
                          self->stack,
                          self->stack_size,
                          ao_sensor_matrix_thread_entry,
                          self, NULL, NULL,
                          K_PRIO_PREEMPT(AO_SENSOR_MATRIX_PRIORITY),
                          0,
                          K_NO_WAIT);

    (void)k_thread_name_set(&self->thread, "ao_sensor_matrix");

    return 0;
}

int ao_sensor_matrix_start(ao_sensor_matrix_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    k_msgq_purge(self->queue);

    self->is_running = true;
    (void)k_sem_give(&self->run_sem);

    return 0;
}

int ao_sensor_matrix_trigger(ao_sensor_matrix_t *self)
{
    if (self == NULL || !self->is_initialized || self->queue == NULL) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return -EAGAIN;
    }

    const ao_sensor_matrix_event_t evt = {
        .type = AO_SENSOR_MATRIX_EVENT_UPDATE_NOW,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_sensor_matrix_step(ao_sensor_matrix_t *self, uint32_t elapsed_ms)
{
    ARG_UNUSED(elapsed_ms);

    if (self == NULL || !self->is_initialized || self->matrix == NULL) {
        return -EINVAL;
    }

    int err = i_sensor_matrix_update(self->matrix);
    if (err != 0) {
        self->error_count++;
        LOG_WRN("sensor matrix update failed (%d, consecutive %u)",
                err, (unsigned int)self->error_count);
        return err;
    }

    self->error_count = 0U;

    /* 1. Read semantic matrix frame and publish to chan_thermal_matrix */
    thermal_matrix_frame_t frame;
    err = i_sensor_matrix_get_frame(self->matrix, &frame);
    if (err == 0) {
        (void)zbus_chan_pub(&chan_thermal_matrix, &frame, K_MSEC(50));
    } else {
        LOG_WRN("sensor matrix frame unavailable (%d)", err);
    }

    /* 2. Obtain raw readings, pack flat snapshot, and publish to chan_sensor_snapshot */
    raw_sensor_readings_t raw;
    err = i_sensor_matrix_get_raw_readings(self->matrix, &raw);
    if (err == 0) {
        sensor_snapshot_msg_t snapshot;
        (void)memset(&snapshot, 0, sizeof(snapshot));
        int pack_res = sensor_matrix_pack_flat_snapshot(&raw, snapshot.temperatures, &snapshot.valid_mask);
        if (pack_res == 0) {
            snapshot.timestamp_ms = (uint64_t)k_uptime_get();
            snapshot.sequence = self->sequence++;
            (void)zbus_chan_pub(&chan_sensor_snapshot, &snapshot, K_MSEC(50));
        } else {
            LOG_WRN("failed to pack flat snapshot (%d)", pack_res);
        }
    } else {
        LOG_WRN("raw sensor readings unavailable (%d)", err);
    }

    return 0;
}

int ao_sensor_matrix_stop(ao_sensor_matrix_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;

    const ao_sensor_matrix_event_t evt = {
        .type = AO_SENSOR_MATRIX_EVENT_STOP,
    };
    (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);

    return 0;
}

int ao_sensor_matrix_wait_stopped(ao_sensor_matrix_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_sensor_matrix_deinit(ao_sensor_matrix_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY;
    }

    self->terminate = true;
    (void)k_sem_give(&self->run_sem);

    int err = k_thread_join(&self->thread, K_MSEC(AO_SENSOR_MATRIX_STOP_WAIT_MS));
    if (err != 0) {
        LOG_WRN("ao_sensor_matrix deinit timeout, forcing abort");
        (void)k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}
