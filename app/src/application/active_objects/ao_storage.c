/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Active Object coordinator for persistent storage implementation.
 */

#include "application/active_objects/ao_storage.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_storage, LOG_LEVEL_INF);

static void ao_storage_publish_event(config_domain_t domain,
                                    config_event_type_t event_type,
                                    uint16_t sub_id,
                                    int32_t status)
{
    system_config_event_t evt = {
        .domain = domain,
        .event_type = event_type,
        .sub_id = sub_id,
        .status = status,
    };
    (void)zbus_chan_pub(&chan_system_config, &evt, K_MSEC(50));
}

int ao_storage_sync_flush(ao_storage_t *self)
{
    if (self == NULL || self->storage == NULL) {
        return -EINVAL;
    }

    int persisted_count = 0;

    for (int d = 0; d < (int)CONFIG_DOMAIN_MAX; d++) {
        config_domain_t domain = (config_domain_t)d;
        uint32_t bit = (1U << domain);

        if ((self->dirty_mask & bit) == 0) {
            continue;
        }

        ao_storage_provider_entry_t *provider = &self->providers[domain];
        if (provider->fn == NULL) {
            LOG_WRN("Domain %d is dirty but no provider registered", domain);
            self->dirty_mask &= ~bit;
            ao_storage_publish_event(domain, CONFIG_EVENT_ERROR, 0, -ENODATA);
            continue;
        }

        size_t size = 0;
        int err = provider->fn(provider->ctx, domain, self->bounce_buf,
                               sizeof(self->bounce_buf), &size);
        if (err != 0 || size == 0) {
            LOG_ERR("Provider for domain %d failed to retrieve data: %d", domain, err);
            self->dirty_mask &= ~bit;
            ao_storage_publish_event(domain, CONFIG_EVENT_ERROR, 0, err != 0 ? err : -ENODATA);
            continue;
        }

        err = i_config_storage_save(self->storage, domain, self->bounce_buf, size);
        if (err != 0) {
            LOG_ERR("Failed to persist domain %d: %d", domain, err);
            self->dirty_mask &= ~bit;
            ao_storage_publish_event(domain, CONFIG_EVENT_ERROR, 0, err);
            continue;
        }

        self->dirty_mask &= ~bit;
        persisted_count++;
        LOG_INF("Persisted domain %d (%u bytes)", domain, (unsigned int)size);
        ao_storage_publish_event(domain, CONFIG_EVENT_PERSISTED, 0, 0);
    }

    return persisted_count;
}

static void ao_storage_thread(void *p1, void *p2, void *p3)
{
    ao_storage_t *self = (ao_storage_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    bool should_terminate = false;

    while (!should_terminate) {
        ao_storage_event_t event;
        /* Outer gate: wait for START or TERMINATE */
        int err = k_msgq_get(self->queue, &event, K_FOREVER);
        if (err != 0) {
            continue;
        }
        if (event.type == AO_STORAGE_EVENT_TERMINATE) {
            break;
        }
        if (event.type != AO_STORAGE_EVENT_START) {
            continue;
        }

        LOG_INF("ao_storage thread entered inner loop");

        /* Inner work loop */
        while (self->is_running) {
            k_timeout_t wait_time = K_FOREVER;

            if (k_timer_status_get(&self->debounce_timer) > 0) {
                (void)ao_storage_sync_flush(self);
            }

            if (self->dirty_mask != 0) {
                k_ticks_t rem = k_timer_remaining_ticks(&self->debounce_timer);
                wait_time = K_TICKS(rem);
                if (K_TICKS(rem).ticks == 0) {
                    (void)ao_storage_sync_flush(self);
                    wait_time = K_FOREVER;
                }
            }

            err = k_msgq_get(self->queue, &event, wait_time);
            if (err == 0) {
                if (event.type == AO_STORAGE_EVENT_STOP) {
                    break;
                }
                if (event.type == AO_STORAGE_EVENT_TERMINATE) {
                    should_terminate = true;
                    break;
                }
                if (event.type == AO_STORAGE_EVENT_DIRTY) {
                    if (config_domain_is_valid(event.domain)) {
                        self->dirty_mask |= (1U << event.domain);
                        k_timer_start(&self->debounce_timer, K_MSEC(AO_STORAGE_DEBOUNCE_MS), K_NO_WAIT);
                        LOG_DBG("Domain %d marked dirty, debounce timer restarted", event.domain);
                    }
                } else if (event.type == AO_STORAGE_EVENT_FLUSH_NOW) {
                    k_timer_stop(&self->debounce_timer);
                    (void)ao_storage_sync_flush(self);
                }
            } else if (err == -EAGAIN) {
                (void)ao_storage_sync_flush(self);
            }
        }

        /* Thread exiting inner loop, flush remaining dirty data */
        k_timer_stop(&self->debounce_timer);
        (void)ao_storage_sync_flush(self);

        self->is_running = false;
        LOG_INF("ao_storage thread exited inner loop");
        if (!should_terminate) {
            (void)k_sem_give(&self->stopped_sem);
        }
    }

    LOG_INF("ao_storage thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_storage_init(ao_storage_t *self,
                    const i_config_storage_t *storage,
                    struct k_msgq *queue,
                    k_thread_stack_t *stack,
                    size_t stack_size)
{
    if (self == NULL || storage == NULL || queue == NULL || stack == NULL || stack_size == 0) {
        return -EINVAL;
    }

    if (!i_config_storage_is_bound(storage)) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->storage = storage;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;
    self->dirty_mask = 0;

    k_sem_init(&self->stopped_sem, 0, 1);
    k_timer_init(&self->debounce_timer, NULL, NULL);

    self->is_initialized = true;

    /* Spawn worker thread once in gated state */
    k_thread_create(&self->thread,
                    self->stack,
                    self->stack_size,
                    ao_storage_thread,
                    self, NULL, NULL,
                    K_PRIO_PREEMPT(AO_STORAGE_PRIORITY),
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&self->thread, "ao_storage");

    return 0;
}

int ao_storage_register_provider(ao_storage_t *self,
                                config_domain_t domain,
                                ao_storage_domain_provider_fn provider,
                                void *ctx)
{
    if (self == NULL || !config_domain_is_valid(domain) || provider == NULL) {
        return -EINVAL;
    }

    self->providers[domain].fn = provider;
    self->providers[domain].ctx = ctx;
    return 0;
}

int ao_storage_start(ao_storage_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EALREADY;
    }

    self->is_running = true;

    ao_storage_event_t evt = {
        .type = AO_STORAGE_EVENT_START,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_storage_post(ao_storage_t *self, const ao_storage_event_t *event)
{
    if (self == NULL || event == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (!self->is_running) {
        return -EAGAIN;
    }

    int err = k_msgq_put(self->queue, event, K_NO_WAIT);
    if (err != 0) {
        return -ENOMSG;
    }
    return 0;
}

int ao_storage_mark_dirty(ao_storage_t *self, config_domain_t domain, uint16_t sub_id)
{
    if (self == NULL || !config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    ao_storage_event_t evt = {
        .type = AO_STORAGE_EVENT_DIRTY,
        .domain = domain,
        .sub_id = sub_id,
    };
    return ao_storage_post(self, &evt);
}

int ao_storage_flush_now(ao_storage_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    ao_storage_event_t evt = {
        .type = AO_STORAGE_EVENT_FLUSH_NOW,
    };
    return ao_storage_post(self, &evt);
}

int ao_storage_stop(ao_storage_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;

    ao_storage_event_t evt = {
        .type = AO_STORAGE_EVENT_STOP,
    };
    (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);

    return 0;
}

int ao_storage_wait_stopped(ao_storage_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_storage_deinit(ao_storage_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY;
    }

    ao_storage_event_t evt = {
        .type = AO_STORAGE_EVENT_TERMINATE,
    };
    (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_storage deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}
