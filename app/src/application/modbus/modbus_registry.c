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

static void store_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t) (value & 0xFFU);
    dst[1] = (uint8_t) ((value >> 8) & 0xFFU);
}

static uint16_t load_le16(const uint8_t *src)
{
    return (uint16_t) ((uint16_t) src[0] | ((uint16_t) src[1] << 8));
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

int modbus_registry_set_bus(modbus_registry_t *self, uint8_t device_id, uint8_t new_bus_id)
{
    if (self == NULL || !bus_is_valid(new_bus_id)) {
        return -EINVAL;
    }

    int ret = 0;

    (void) k_mutex_lock(&self->lock, K_FOREVER);

    modbus_slave_snapshot_t *slot = find_by_device_id(self, device_id);

    if (slot == NULL) {
        ret = -ENOENT;
    } else if (slot->descriptor.bus_id == new_bus_id) {
        ret = 0;
    } else if (address_in_use(self, new_bus_id, slot->descriptor.slave_addr, device_id)) {
        ret = -EADDRINUSE;
    } else if (count_on_bus(self, new_bus_id) >= MODBUS_MAX_SLAVES_PER_BUS) {
        ret = -ENOSPC;
    } else {
        slot->descriptor.bus_id = new_bus_id;
        /* The device now answers on another wire: restart its schedule. */
        slave_scheduler_reset(&slot->runtime);
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
 * PERSISTENCE
 *============================================================================*/

int modbus_registry_export(const modbus_registry_t *self, void *buf, size_t max,
                           size_t *out_size)
{
    if (self == NULL || buf == NULL || out_size == NULL) {
        return -EINVAL;
    }

    uint8_t *dst = (uint8_t *) buf;
    size_t offset = 0U;

    modbus_registry_t *mutable_self = (modbus_registry_t *) self;

    (void) k_mutex_lock(&mutable_self->lock, K_FOREVER);

    /* Captured once: the table must not grow between the size check and the
     * copy loop, or the records would be written past the caller's buffer. */
    const size_t records = mutable_self->count;

    size_t needed = MODBUS_REGISTRY_EXPORT_HEADER_SIZE +
                    (records * MODBUS_REGISTRY_EXPORT_RECORD_SIZE);

    if (max < needed) {
        (void) k_mutex_unlock(&mutable_self->lock);

        return -ENOSPC;
    }

    dst[offset++] = (uint8_t) MODBUS_REGISTRY_EXPORT_VERSION;
    dst[offset++] = (uint8_t) MODBUS_BUS_MAX;

    for (uint8_t bus = 0U; bus < (uint8_t) MODBUS_BUS_MAX; bus++) {
        const modbus_bus_cfg_t *cfg = &mutable_self->bus_cfg[bus];

        dst[offset++] = (uint8_t) (cfg->baudrate & 0xFFU);
        dst[offset++] = (uint8_t) ((cfg->baudrate >> 8) & 0xFFU);
        dst[offset++] = (uint8_t) ((cfg->baudrate >> 16) & 0xFFU);
        dst[offset++] = (uint8_t) ((cfg->baudrate >> 24) & 0xFFU);
        dst[offset++] = (uint8_t) cfg->parity;
        dst[offset++] = cfg->stop_bits;
        dst[offset++] = cfg->data_bits;
    }

    store_le16(&dst[offset], (uint16_t) records);
    offset += 2U;

    for (size_t i = 0U; i < records; i++) {
        const modbus_slave_descriptor_t *desc = &mutable_self->entries[i].descriptor;

        dst[offset++] = desc->device_id;
        dst[offset++] = desc->bus_id;
        dst[offset++] = desc->slave_addr;
        dst[offset++] = desc->dev_type;
        store_le16(&dst[offset], desc->poll_interval_ms);
        offset += 2U;
        store_le16(&dst[offset], desc->timeout_ms);
        offset += 2U;
        dst[offset++] = desc->is_enabled ? 1U : 0U;
    }

    (void) k_mutex_unlock(&mutable_self->lock);

    *out_size = offset;

    return 0;
}

int modbus_registry_import(modbus_registry_t *self, const void *buf, size_t size)
{
    if (self == NULL || buf == NULL) {
        return -EINVAL;
    }

    const uint8_t *src = (const uint8_t *) buf;

    if (size < MODBUS_REGISTRY_EXPORT_HEADER_SIZE) {
        return -EINVAL;
    }

    if (src[0] != (uint8_t) MODBUS_REGISTRY_EXPORT_VERSION) {
        return -EINVAL;
    }

    if (src[1] != (uint8_t) MODBUS_BUS_MAX) {
        return -EINVAL;
    }

    modbus_bus_cfg_t bus_cfg[MODBUS_BUS_MAX];
    size_t offset = 2U;

    for (uint8_t bus = 0U; bus < (uint8_t) MODBUS_BUS_MAX; bus++) {
        bus_cfg[bus].baudrate = (uint32_t) src[offset] | ((uint32_t) src[offset + 1U] << 8) |
                                ((uint32_t) src[offset + 2U] << 16) |
                                ((uint32_t) src[offset + 3U] << 24);
        bus_cfg[bus].parity = (modbus_parity_t) src[offset + 4U];
        bus_cfg[bus].stop_bits = src[offset + 5U];
        bus_cfg[bus].data_bits = src[offset + 6U];
        offset += 7U;

        if (!modbus_bus_cfg_is_valid(&bus_cfg[bus])) {
            return -EINVAL;
        }
    }

    uint16_t record_count = load_le16(&src[offset]);
    offset += 2U;

    if (record_count > MODBUS_MAX_TOTAL_SLAVES) {
        return -EINVAL;
    }

    if (size < offset + ((size_t) record_count * MODBUS_REGISTRY_EXPORT_RECORD_SIZE)) {
        return -EINVAL;
    }

    /* Validate the whole blob before touching the table. */
    size_t per_bus[MODBUS_BUS_MAX] = {0U};

    for (uint16_t i = 0U; i < record_count; i++) {
        const uint8_t *record = &src[offset + ((size_t) i * MODBUS_REGISTRY_EXPORT_RECORD_SIZE)];
        modbus_slave_descriptor_t desc = {
            .device_id = record[0],
            .bus_id = record[1],
            .slave_addr = record[2],
            .dev_type = record[3],
            .poll_interval_ms = load_le16(&record[4]),
            .timeout_ms = load_le16(&record[6]),
            .is_enabled = (record[8] != 0U),
        };

        if (!modbus_slave_descriptor_is_valid(&desc)) {
            return -EINVAL;
        }

        if (++per_bus[desc.bus_id] > MODBUS_MAX_SLAVES_PER_BUS) {
            return -EINVAL;
        }

        for (uint16_t j = 0U; j < i; j++) {
            const uint8_t *other =
                &src[offset + ((size_t) j * MODBUS_REGISTRY_EXPORT_RECORD_SIZE)];

            if (other[0] == desc.device_id) {
                return -EINVAL;
            }

            if (other[1] == desc.bus_id && other[2] == desc.slave_addr) {
                return -EINVAL;
            }
        }
    }

    /* Commit. */
    (void) k_mutex_lock(&self->lock, K_FOREVER);

    (void) memcpy(self->bus_cfg, bus_cfg, sizeof(self->bus_cfg));
    (void) memset(self->entries, 0, sizeof(self->entries));
    (void) memset(self->cursor, 0, sizeof(self->cursor));

    for (uint16_t i = 0U; i < record_count; i++) {
        const uint8_t *record = &src[offset + ((size_t) i * MODBUS_REGISTRY_EXPORT_RECORD_SIZE)];
        modbus_slave_snapshot_t *slot = &self->entries[i];

        slot->descriptor.device_id = record[0];
        slot->descriptor.bus_id = record[1];
        slot->descriptor.slave_addr = record[2];
        slot->descriptor.dev_type = record[3];
        slot->descriptor.poll_interval_ms = load_le16(&record[4]);
        slot->descriptor.timeout_ms = load_le16(&record[6]);
        slot->descriptor.is_enabled = (record[8] != 0U);

        slave_scheduler_reset(&slot->runtime);
    }

    self->count = record_count;

    (void) k_mutex_unlock(&self->lock);

    return 0;
}

/*============================================================================*
 * PORT ADAPTER
 *============================================================================*/

static int port_register_device(void *impl, const modbus_slave_descriptor_t *desc)
{
    return modbus_registry_register((modbus_registry_t *) impl, desc);
}

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

static int port_set_bus(void *impl, uint8_t device_id, uint8_t new_bus_id)
{
    return modbus_registry_set_bus((modbus_registry_t *) impl, device_id, new_bus_id);
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
    .register_device = port_register_device,
    .claim_next_due = port_claim_next_due,
    .next_due_in = port_next_due_in,
    .record = port_record,
    .set_bus = port_set_bus,
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
