/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_digital_input.c
 * @brief Active Object coordinator for Digital Inputs and Contact Alarms.
 */

#include "application/active_objects/ao_digital_input.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#if defined(CONFIG_ZBUS)
#include <zephyr/zbus/zbus.h>
#include "application/zbus/digital_input_channels.h"
#endif

LOG_MODULE_REGISTER(ao_digital_input, LOG_LEVEL_INF);

static void ao_digital_input_thread_entry(void *p1, void *p2, void *p3)
{
    ao_digital_input_t *self = (ao_digital_input_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!self->terminate) {
        /* 1. Blocked on outer gate until ao_digital_input_start() or deinit() */
        (void)k_sem_take(&self->run_sem, K_FOREVER);
        if (self->terminate) {
            break;
        }

        LOG_INF("ao_digital_input thread entered inner loop");
        self->last_scan_ms = (uint64_t)k_uptime_get();

        /* 2. Inner work loop */
        while (self->is_running) {
            if (self->queue != NULL) {
                ao_di_event_t evt;
                int ret = k_msgq_get(self->queue, &evt, K_MSEC(AO_DIGITAL_INPUT_SCAN_PERIOD_MS));
                if (ret == 0) {
                    if (evt.type == AO_DI_EVENT_STOP) {
                        break;
                    }
                    /* AO_DI_EVENT_SCAN_NOW triggers immediate scan */
                }
            } else {
                k_msleep(AO_DIGITAL_INPUT_SCAN_PERIOD_MS);
            }

            if (!self->is_running) {
                break;
            }

            uint64_t now_ms = (uint64_t)k_uptime_get();
            uint32_t elapsed_ms = (self->last_scan_ms > 0ULL && now_ms >= self->last_scan_ms)
                                    ? (uint32_t)(now_ms - self->last_scan_ms)
                                    : AO_DIGITAL_INPUT_SCAN_PERIOD_MS;
            if (elapsed_ms == 0U) {
                elapsed_ms = AO_DIGITAL_INPUT_SCAN_PERIOD_MS;
            }
            self->last_scan_ms = now_ms;

            (void)ao_digital_input_step(self, elapsed_ms);
        }

        LOG_INF("ao_digital_input thread exited inner loop");
        self->is_running = false;
        (void)k_sem_give(&self->stopped_sem);
    }

    LOG_INF("ao_digital_input thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_digital_input_init(ao_digital_input_t *self,
                          di_aggregator_t *aggregator,
                          struct k_msgq *queue,
                          k_thread_stack_t *stack,
                          size_t stack_size)
{
    if (self == NULL || aggregator == NULL || queue == NULL ||
        stack == NULL || stack_size == 0U) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->aggregator = aggregator;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;
    self->terminate = false;
    self->last_scan_ms = 0ULL;

    k_sem_init(&self->run_sem, 0, 1);
    k_sem_init(&self->stopped_sem, 0, 1);

    self->is_initialized = true;

    /* Spawn thread once in gated state */
    k_thread_create(&self->thread,
                    self->stack,
                    self->stack_size,
                    ao_digital_input_thread_entry,
                    self, NULL, NULL,
                    K_PRIO_PREEMPT(AO_DIGITAL_INPUT_PRIORITY),
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&self->thread, "ao_digital_input");

    return 0;
}

int ao_digital_input_start(ao_digital_input_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    if (self->queue != NULL) {
        k_msgq_purge(self->queue);
    }

    self->is_running = true;
    k_sem_give(&self->run_sem);

    return 0;
}

int ao_digital_input_scan_now(ao_digital_input_t *self)
{
    if (self == NULL || self->queue == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return -EAGAIN;
    }

    ao_di_event_t evt = {
        .type = AO_DI_EVENT_SCAN_NOW,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_digital_input_stop(ao_digital_input_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;

    if (self->queue != NULL) {
        ao_di_event_t evt = {
            .type = AO_DI_EVENT_STOP,
        };
        (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);
    }

    return 0;
}

int ao_digital_input_wait_stopped(ao_digital_input_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_digital_input_deinit(ao_digital_input_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY;
    }

    self->terminate = true;
    k_sem_give(&self->run_sem);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_digital_input deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}

int ao_digital_input_step(ao_digital_input_t *self, uint32_t elapsed_ms)
{
    if (self == NULL || self->aggregator == NULL) {
        return -EINVAL;
    }

    di_transition_event_t events[DI_CHANNEL_MAX];
    size_t event_count = 0U;

    int ret = di_aggregator_scan(self->aggregator, elapsed_ms, events, DI_CHANNEL_MAX, &event_count);
    if (ret != 0) {
        LOG_ERR("di_aggregator_scan failed: %d", ret);
        return ret;
    }

#if defined(CONFIG_ZBUS)
    for (size_t i = 0; i < event_count; i++) {
        int pub_ret = zbus_chan_pub(&chan_digital_input_state, &events[i], K_MSEC(50));
        if (pub_ret != 0) {
            LOG_WRN("zbus pub failed on chan_digital_input_state: %d", pub_ret);
        }
    }
#endif

    return (int)event_count;
}
