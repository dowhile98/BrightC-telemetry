/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Active Object orchestrating Ethernet and WiFi network interfaces.
 */

#include "application/active_objects/ao_network.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_network, LOG_LEVEL_INF);

static bool status_changed(const net_status_t *a, const net_status_t *b)
{
    if (a->state != b->state ||
        a->ip_addr != b->ip_addr ||
        a->subnet_mask != b->subnet_mask ||
        a->gateway != b->gateway ||
        a->rssi != b->rssi) {
        return true;
    }
    return false;
}

int ao_network_sync_step(ao_network_t *self)
{
    if (self == NULL || self->net_eth == NULL || self->net_wifi == NULL) {
        return -EINVAL;
    }

    uint32_t now_ms = (uint32_t)k_uptime_get();

    /* 1. Poll Ethernet status */
    net_status_t eth_status;
    int err = i_network_get_status(self->net_eth, NETWORK_INTERFACE_ETHERNET, &eth_status);
    if (err == 0) {
        if (!self->has_eth_status || status_changed(&self->last_eth_status, &eth_status)) {
            self->last_eth_status = eth_status;
            self->has_eth_status = true;

            net_state_msg_t msg = {
                .interface = NETWORK_INTERFACE_ETHERNET,
                .status = eth_status,
                .timestamp_ms = now_ms,
            };
            (void)zbus_chan_pub(&chan_network_eth_state, &msg, K_MSEC(50));
            LOG_DBG("Published eth state=%d, ip=0x%08X", (int)eth_status.state, (unsigned int)eth_status.ip_addr);
        }
    }

    /* 2. Poll WiFi status */
    net_status_t wifi_status;
    err = i_network_get_status(self->net_wifi, NETWORK_INTERFACE_WIFI, &wifi_status);
    if (err == 0) {
        if (!self->has_wifi_status || status_changed(&self->last_wifi_status, &wifi_status)) {
            self->last_wifi_status = wifi_status;
            self->has_wifi_status = true;

            net_state_msg_t msg = {
                .interface = NETWORK_INTERFACE_WIFI,
                .status = wifi_status,
                .timestamp_ms = now_ms,
            };
            (void)zbus_chan_pub(&chan_network_wifi_state, &msg, K_MSEC(50));
            LOG_DBG("Published wifi state=%d, ip=0x%08X, rssi=%d", (int)wifi_status.state, (unsigned int)wifi_status.ip_addr, (int)wifi_status.rssi);
        }
    }

    return 0;
}

static void ao_network_thread(void *p1, void *p2, void *p3)
{
    ao_network_t *self = (ao_network_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("ao_network thread started");

    bool should_terminate = false;
    while (!should_terminate) {
        ao_network_event_t event;
        int err = k_msgq_get(self->queue, &event, K_FOREVER);
        if (err != 0) {
            continue;
        }
        if (event.type == AO_NETWORK_EVENT_TERMINATE) {
            break;
        }
        if (event.type != AO_NETWORK_EVENT_START) {
            continue;
        }

        LOG_INF("ao_network thread entered inner loop");

        while (self->is_running) {
            err = k_msgq_get(self->queue, &event, K_MSEC(AO_NETWORK_POLL_INTERVAL_MS));
            if (err == 0) {
                if (event.type == AO_NETWORK_EVENT_STOP) {
                    break;
                }
                if (event.type == AO_NETWORK_EVENT_TERMINATE) {
                    should_terminate = true;
                    break;
                }

                if (event.type == AO_NETWORK_EVENT_CONFIG_ETH) {
                    self->eth_cfg = event.data.eth_cfg;
                    (void)i_network_stop(self->net_eth, NETWORK_INTERFACE_ETHERNET);
                    (void)i_network_set_config(self->net_eth, NETWORK_INTERFACE_ETHERNET, &self->eth_cfg, sizeof(self->eth_cfg));
                    (void)i_network_start(self->net_eth, NETWORK_INTERFACE_ETHERNET);
                } else if (event.type == AO_NETWORK_EVENT_CONFIG_WIFI) {
                    self->wifi_cfg = event.data.wifi_cfg;
                    (void)i_network_stop(self->net_wifi, NETWORK_INTERFACE_WIFI);
                    (void)i_network_set_config(self->net_wifi, NETWORK_INTERFACE_WIFI, &self->wifi_cfg, sizeof(self->wifi_cfg));
                    (void)i_network_start(self->net_wifi, NETWORK_INTERFACE_WIFI);
                }
            }

            /* Periodic status check & publish if state changed */
            (void)ao_network_sync_step(self);
        }

        /* Stop network interfaces */
        if (self->net_eth) {
            (void)i_network_stop(self->net_eth, NETWORK_INTERFACE_ETHERNET);
        }
        if (self->net_wifi) {
            (void)i_network_stop(self->net_wifi, NETWORK_INTERFACE_WIFI);
        }

        self->is_running = false;
        LOG_INF("ao_network thread exited inner loop");
        if (!should_terminate) {
            (void)k_sem_give(&self->stopped_sem);
        }
    }

    LOG_INF("ao_network thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_network_init(ao_network_t *self,
                    const i_network_t *net_eth,
                    const i_network_t *net_wifi,
                    const net_eth_config_t *initial_eth_cfg,
                    const net_wifi_config_t *initial_wifi_cfg,
                    struct k_msgq *queue,
                    k_thread_stack_t *stack,
                    size_t stack_size)
{
    if (self == NULL || net_eth == NULL || net_wifi == NULL ||
        queue == NULL || stack == NULL || stack_size == 0U) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->net_eth = net_eth;
    self->net_wifi = net_wifi;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;

    k_sem_init(&self->stopped_sem, 0, 1);
    self->is_initialized = true;

    /* Initialize configurations: use injected initial configs if provided, else fall back to domain defaults */
    if (initial_eth_cfg != NULL) {
        self->eth_cfg = *initial_eth_cfg;
    } else {
        (void)network_default_eth_config(&self->eth_cfg);
    }

    if (initial_wifi_cfg != NULL) {
        self->wifi_cfg = *initial_wifi_cfg;
    } else {
        (void)network_default_wifi_config(&self->wifi_cfg);
    }

    (void)k_thread_create(&self->thread, self->stack, self->stack_size, ao_network_thread,
                          self, NULL, NULL, K_PRIO_PREEMPT(AO_NETWORK_PRIORITY), 0, K_NO_WAIT);
    (void)k_thread_name_set(&self->thread, "ao_network");

    return 0;
}

int ao_network_start(ao_network_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    /* 1. Initialize both adapters */
    (void)i_network_init(self->net_eth, NETWORK_INTERFACE_ETHERNET);
    (void)i_network_init(self->net_wifi, NETWORK_INTERFACE_WIFI);

    /* 2. Configure both interfaces with default settings */
    (void)i_network_set_config(self->net_eth, NETWORK_INTERFACE_ETHERNET, &self->eth_cfg, sizeof(self->eth_cfg));
    (void)i_network_set_config(self->net_wifi, NETWORK_INTERFACE_WIFI, &self->wifi_cfg, sizeof(self->wifi_cfg));

    /* 3. Start both interfaces */
    (void)i_network_start(self->net_eth, NETWORK_INTERFACE_ETHERNET);
    (void)i_network_start(self->net_wifi, NETWORK_INTERFACE_WIFI);

    /* Purge stale messages in queue */
    k_msgq_purge(self->queue);
    self->is_running = true;

    ao_network_event_t evt = {
        .type = AO_NETWORK_EVENT_START,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_network_post(ao_network_t *self, const ao_network_event_t *event)
{
    if (self == NULL || event == NULL || self->queue == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (k_msgq_put(self->queue, event, K_NO_WAIT) != 0) {
        return -ENOMSG;
    }

    return 0;
}

int ao_network_stop(ao_network_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    const ao_network_event_t stop_evt = {.type = AO_NETWORK_EVENT_STOP};
    self->is_running = false;
    return ao_network_post(self, &stop_evt);
}

int ao_network_wait_stopped(ao_network_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_network_deinit(ao_network_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY;
    }

    const ao_network_event_t term_evt = {.type = AO_NETWORK_EVENT_TERMINATE};
    (void)k_msgq_put(self->queue, &term_evt, K_NO_WAIT);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_network deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}
