/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "modbus_registry.h"

#include <errno.h>
#include <string.h>

#include "domain/services/slave_scheduler.h"

/*============================================================================*
 * INTERNAL HELPERS (callers already hold the lock)
 *============================================================================*/

static bool bus_is_valid(uint8_t bus_id)
{
    return bus_id < (uint8_t) MODBUS_BUS_MAX;
}

static size_t count_on_bus(const modbus_registry_t *self, uint8_t bus_id)
{
    size_t total = 0U;

    for (size_t i = 0U; i < self->count; i++) {
        if (self->entries[i].descriptor.bus_id == bus_id) {
            total++;
        }
    }

    return total;
}

static modbus_slave_snapshot_t *find_by_device_id(modbus_registry_t *self, uint8_t device_id)
{
    for (size_t i = 0U; i < self->count; i++) {
        if (self->entries[i].descriptor.device_id == device_id) {
            return &self->entries[i];
        }
    }

    return NULL;
}

static bool address_in_use(const modbus_registry_t *self, uint8_t bus_id, uint8_t slave_addr,
                           uint8_t ignored_device_id)
{
    for (size_t i = 0U; i < self->count; i++) {
        const modbus_slave_descriptor_t *desc = &self->entries[i].descriptor;

        if (desc->bus_id == bus_id && desc->slave_addr == slave_addr &&
            desc->device_id != ignored_device_id) {
            return true;
        }
    }

    return false;
}

/*============================================================================*
 * LIFECYCLE
 *============================================================================*/

int modbus_registry_init(modbus_registry_t *self, const modbus_bus_cfg_t cfg[MODBUS_BUS_MAX])
{
    if (self == NULL || cfg == NULL) {
        return -EINVAL;
    }

    for (uint8_t bus = 0U; bus < (uint8_t) MODBUS_BUS_MAX; bus++) {
        if (!modbus_bus_cfg_is_valid(&cfg[bus])) {
            return -EINVAL;
        }
    }

    (void) memset(self, 0, sizeof(*self));
    (void) memcpy(self->bus_cfg, cfg, sizeof(self->bus_cfg));

    /* A freshly zeroed runtime already has ONLINE health, a zeroed cursor and
     * no claim, which is why there is no per-slot initialisation here. */
    for (size_t i = 0U; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        slave_scheduler_reset(&self->entries[i].runtime);
    }

    (void) k_mutex_init(&self->lock);

    return 0;
}

/*============================================================================*
 * TABLE MANAGEMENT
 *============================================================================*/

int modbus_registry_register(modbus_registry_t *self, const modbus_slave_descriptor_t *desc)
{
    if (self == NULL || !modbus_slave_descriptor_is_valid(desc)) {
        return -EINVAL;
    }

    int ret = 0;

    (void) k_mutex_lock(&self->lock, K_FOREVER);

    modbus_slave_snapshot_t *existing = find_by_device_id(self, desc->device_id);

    if (existing != NULL) {
        bool identity_changed = (existing->descriptor.bus_id != desc->bus_id) ||
                                (existing->descriptor.slave_addr != desc->slave_addr);

        if (identity_changed) {
            if (address_in_use(self, desc->bus_id, desc->slave_addr, desc->device_id)) {
                ret = -EADDRINUSE;
            } else if (existing->descriptor.bus_id != desc->bus_id &&
                       count_on_bus(self, desc->bus_id) >= MODBUS_MAX_SLAVES_PER_BUS) {
                ret = -ENOSPC;
            } else {
                slave_scheduler_reset(&existing->runtime);
            }
        }

        if (ret == 0) {
            existing->descriptor = *desc;
        }
    } else {
        if (address_in_use(self, desc->bus_id, desc->slave_addr, desc->device_id)) {
            ret = -EADDRINUSE;
        } else if (count_on_bus(self, desc->bus_id) >= MODBUS_MAX_SLAVES_PER_BUS) {
            ret = -ENOSPC;
        } else if (self->count >= MODBUS_MAX_TOTAL_SLAVES) {
            ret = -ENOSPC;
        } else {
            modbus_slave_snapshot_t *slot = &self->entries[self->count];

            slot->descriptor = *desc;
            slave_scheduler_reset(&slot->runtime);
            self->count++;
        }
    }

    (void) k_mutex_unlock(&self->lock);

    return ret;
}

int modbus_registry_get(const modbus_registry_t *self, uint8_t device_id,
                        modbus_slave_descriptor_t *out)
{
    if (self == NULL || out == NULL) {
        return -EINVAL;
    }

    int ret = -ENOENT;

    modbus_registry_t *mutable_self = (modbus_registry_t *) self;

    (void) k_mutex_lock(&mutable_self->lock, K_FOREVER);

    const modbus_slave_snapshot_t *slot = find_by_device_id(mutable_self, device_id);

    if (slot != NULL) {
        *out = slot->descriptor;
        ret = 0;
    }

    (void) k_mutex_unlock(&mutable_self->lock);

    return ret;
}

int modbus_registry_find_by_type(const modbus_registry_t *self, modbus_device_type_t dev_type,
                                 modbus_slave_descriptor_t *out)
{
    if (self == NULL || out == NULL) {
        return -EINVAL;
    }

    int ret = -ENOENT;
    modbus_registry_t *mutable_self = (modbus_registry_t *) self;

    (void) k_mutex_lock(&mutable_self->lock, K_FOREVER);

    for (size_t i = 0U; i < mutable_self->count; i++) {
        if (mutable_self->entries[i].descriptor.dev_type == dev_type) {
            *out = mutable_self->entries[i].descriptor;
            ret = 0;
            break;
        }
    }

    (void) k_mutex_unlock(&mutable_self->lock);

    return ret;
}

int modbus_registry_get_bus_cfg(const modbus_registry_t *self, uint8_t bus_id,
                                modbus_bus_cfg_t *out)
{
    if (self == NULL || out == NULL || !bus_is_valid(bus_id)) {
        return -EINVAL;
    }

    modbus_registry_t *mutable_self = (modbus_registry_t *) self;

    (void) k_mutex_lock(&mutable_self->lock, K_FOREVER);
    *out = mutable_self->bus_cfg[bus_id];
    (void) k_mutex_unlock(&mutable_self->lock);

    return 0;
}

int modbus_registry_snapshot(modbus_registry_t *self, uint8_t bus_id,
                             modbus_slave_snapshot_t *out, size_t max, size_t *out_count)
{
    if (self == NULL || out == NULL || out_count == NULL || !bus_is_valid(bus_id) || max == 0U) {
        return -EINVAL;
    }

    size_t written = 0U;

    (void) k_mutex_lock(&self->lock, K_FOREVER);

    for (size_t i = 0U; i < self->count && written < max; i++) {
        if (self->entries[i].descriptor.bus_id == bus_id) {
            out[written] = self->entries[i];
            written++;
        }
    }

    (void) k_mutex_unlock(&self->lock);

    *out_count = written;

    return 0;
}

/*============================================================================*
 * SCHEDULING
 *============================================================================*/

int modbus_registry_claim_next_due(modbus_registry_t *self, uint8_t bus_id, uint64_t now_ms,
                                   modbus_poll_target_t *out)
{
    if (self == NULL || out == NULL || !bus_is_valid(bus_id)) {
        return -EINVAL;
    }

    int ret = -ENOENT;

    (void) k_mutex_lock(&self->lock, K_FOREVER);

    if (self->count > 0U) {
        size_t start = (size_t) self->cursor[bus_id] % self->count;

        for (size_t step = 0U; step < self->count; step++) {
            size_t index = (start + step) % self->count;
            modbus_slave_snapshot_t *slot = &self->entries[index];

            if (slot->descriptor.bus_id != bus_id) {
                continue;
            }

            if (!slave_scheduler_is_due(&slot->descriptor, &slot->runtime, now_ms)) {
                continue;
            }

            slot->runtime.in_flight = true;
            slot->runtime.last_attempt_ms = now_ms;

            out->device_id = slot->descriptor.device_id;
            out->slave_addr = slot->descriptor.slave_addr;
            out->dev_type = slot->descriptor.dev_type;
            out->timeout_ms = slot->descriptor.timeout_ms;

            self->cursor[bus_id] = (uint8_t) ((index + 1U) % self->count);
            ret = 0;
            break;
        }
    }

    (void) k_mutex_unlock(&self->lock);

    return ret;
}

int modbus_registry_record(modbus_registry_t *self, uint8_t device_id,
                           modbus_transaction_result_t result, uint64_t now_ms)
{
    if (self == NULL) {
        return -EINVAL;
    }

    int ret = -ENOENT;

    (void) k_mutex_lock(&self->lock, K_FOREVER);

    modbus_slave_snapshot_t *slot = find_by_device_id(self, device_id);

    if (slot != NULL) {
        slave_scheduler_record(&slot->runtime, result, now_ms);
        ret = 0;
    }

    (void) k_mutex_unlock(&self->lock);

    return ret;
}

int modbus_registry_next_due_in(const modbus_registry_t *self, uint8_t bus_id, uint64_t now_ms,
                                uint32_t *out_ms)
{
    if (self == NULL || out_ms == NULL || !bus_is_valid(bus_id)) {
        return -EINVAL;
    }

    bool found = false;
    uint64_t best = UINT64_MAX;

    modbus_registry_t *mutable_self = (modbus_registry_t *) self;

    (void) k_mutex_lock(&mutable_self->lock, K_FOREVER);

    for (size_t i = 0U; i < mutable_self->count; i++) {
        const modbus_slave_snapshot_t *slot = &mutable_self->entries[i];

        if (slot->descriptor.bus_id != bus_id || !slot->descriptor.is_enabled) {
            continue;
        }

        found = true;

        if (slave_scheduler_is_due(&slot->descriptor, &slot->runtime, now_ms)) {
            best = 0U;
            break;
        }

        uint64_t remaining;

        if (slot->runtime.in_flight &&
            (now_ms - slot->runtime.last_attempt_ms) < MODBUS_CLAIM_STALE_MS) {
            /* The claim, not the poll interval, is what holds this slave back. */
            remaining = (uint64_t) MODBUS_CLAIM_STALE_MS -
                        (now_ms - slot->runtime.last_attempt_ms);
        } else if (!slot->runtime.has_polled) {
            remaining = 0U;
        } else {
            uint64_t interval = (uint64_t) slave_scheduler_interval_ms(&slot->descriptor,
                                                                      &slot->runtime);
            uint64_t elapsed = now_ms - slot->runtime.last_poll_timestamp_ms;

            remaining = (elapsed >= interval) ? 0U : (interval - elapsed);
        }

        if (remaining < best) {
            best = remaining;
        }
    }

    (void) k_mutex_unlock(&mutable_self->lock);

    if (!found) {
        return -ENOENT;
    }

    *out_ms = (uint32_t) ((best > UINT32_MAX) ? UINT32_MAX : best);

    return 0;
}

/*============================================================================*
 * PORT ADAPTER
 *============================================================================*/

static int port_claim_next_due(void *impl, uint8_t bus_id, uint64_t now_ms,
                               modbus_poll_target_t *out)
{
    return modbus_registry_claim_next_due((modbus_registry_t *) impl, bus_id, now_ms, out);
}

static int port_record(void *impl, uint8_t device_id, modbus_transaction_result_t result,
                       uint64_t now_ms)
{
    return modbus_registry_record((modbus_registry_t *) impl, device_id, result, now_ms);
}

static int port_next_due_in(void *impl, uint8_t bus_id, uint64_t now_ms, uint32_t *out_ms)
{
    return modbus_registry_next_due_in((const modbus_registry_t *) impl, bus_id, now_ms, out_ms);
}

static int port_get_bus_cfg(void *impl, uint8_t bus_id, modbus_bus_cfg_t *out)
{
    return modbus_registry_get_bus_cfg((const modbus_registry_t *) impl, bus_id, out);
}

static int port_snapshot(void *impl, uint8_t bus_id, modbus_slave_snapshot_t *out, size_t max,
                         size_t *out_count)
{
    return modbus_registry_snapshot((modbus_registry_t *) impl, bus_id, out, max, out_count);
}

static const i_slave_registry_api_t k_registry_port_api = {
    .claim_next_due = port_claim_next_due,
    .next_due_in = port_next_due_in,
    .record = port_record,
    .get_bus_cfg = port_get_bus_cfg,
    .snapshot = port_snapshot,
};

int modbus_registry_port(modbus_registry_t *self, i_slave_registry_t *out)
{
    if (self == NULL || out == NULL) {
        return -EINVAL;
    }

    out->api = &k_registry_port_api;
    out->impl = self;

    return 0;
}
