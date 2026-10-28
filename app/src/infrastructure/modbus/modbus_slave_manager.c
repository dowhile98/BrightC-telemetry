/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "modbus_slave_manager.h"
#include <string.h>
#include <errno.h>

typedef struct {
    modbus_slave_descriptor_t desc;
    modbus_slave_runtime_state_t runtime;
    bool is_allocated;
} slave_entry_t;

static slave_entry_t s_slaves[MODBUS_MAX_TOTAL_SLAVES];

int modbus_slave_manager_init(void)
{
    memset(s_slaves, 0, sizeof(s_slaves));

    /* Register factory default: Device 1 = CWT-TM-16PT on RS485-1 at address 1 */
    modbus_slave_descriptor_t default_dev = {
        .device_id = 1,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000,
        .timeout_ms = 300,
        .is_enabled = true,
    };

    return modbus_slave_manager_register(&default_dev);
}

int modbus_slave_manager_register(const modbus_slave_descriptor_t *desc)
{
    if (desc == NULL || desc->device_id == 0 || desc->bus_id >= MODBUS_BUS_MAX) {
        return -EINVAL;
    }

    /* Check if already exists to update */
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == desc->device_id) {
            s_slaves[i].desc = *desc;
            return 0;
        }
    }

    /* Find free slot */
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (!s_slaves[i].is_allocated) {
            s_slaves[i].desc = *desc;
            memset(&s_slaves[i].runtime, 0, sizeof(s_slaves[i].runtime));
            s_slaves[i].runtime.health = MODBUS_HEALTH_ONLINE;
            s_slaves[i].is_allocated = true;
            return 0;
        }
    }

    return -ENOSPC;
}

int modbus_slave_manager_get(uint8_t device_id, modbus_slave_descriptor_t *out_desc)
{
    if (out_desc == NULL || device_id == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == device_id) {
            *out_desc = s_slaves[i].desc;
            return 0;
        }
    }

    return -ENOENT;
}

int modbus_slave_manager_set_bus(uint8_t device_id, modbus_bus_id_t new_bus_id)
{
    if (device_id == 0 || new_bus_id >= MODBUS_BUS_MAX) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == device_id) {
            s_slaves[i].desc.bus_id = (uint8_t)new_bus_id;
            return 0;
        }
    }

    return -ENOENT;
}

int modbus_slave_manager_get_bus_devices(modbus_bus_id_t bus_id, uint8_t *out_device_ids, 
                                        size_t max_count, size_t *out_count)
{
    if (bus_id >= MODBUS_BUS_MAX || out_device_ids == NULL || out_count == NULL || max_count == 0) {
        return -EINVAL;
    }

    size_t count = 0;
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES && count < max_count; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.is_enabled && s_slaves[i].desc.bus_id == (uint8_t)bus_id) {
            out_device_ids[count++] = s_slaves[i].desc.device_id;
        }
    }

    *out_count = count;
    return 0;
}

void modbus_slave_manager_record_result(uint8_t device_id, bool success, uint64_t now_ms)
{
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == device_id) {
            slave_entry_t *s = &s_slaves[i];
            s->runtime.last_poll_timestamp_ms = now_ms;

            if (success) {
                s->runtime.successful_polls++;
                s->runtime.consecutive_fails = 0;
                s->runtime.last_success_timestamp_ms = now_ms;
                s->runtime.health = MODBUS_HEALTH_ONLINE;
            } else {
                s->runtime.timeout_count++;
                s->runtime.consecutive_fails++;

                if (s->runtime.consecutive_fails >= MODBUS_CONSECUTIVE_FAILS_THRESHOLD) {
                    s->runtime.health = MODBUS_HEALTH_OFFLINE_BACKOFF;
                } else {
                    s->runtime.health = MODBUS_HEALTH_DEGRADED;
                }
            }
            return;
        }
    }
}

bool modbus_slave_manager_is_ready_to_poll(uint8_t device_id, uint64_t now_ms)
{
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == device_id && s_slaves[i].desc.is_enabled) {
            slave_entry_t *s = &s_slaves[i];

            if (s->runtime.last_poll_timestamp_ms == 0) {
                return true; /* Initial poll */
            }

            uint64_t elapsed = now_ms - s->runtime.last_poll_timestamp_ms;

            if (s->runtime.health == MODBUS_HEALTH_OFFLINE_BACKOFF) {
                /* Apply slow backoff interval so we don't clog the bus */
                return (elapsed >= MODBUS_OFFLINE_BACKOFF_INTERVAL_MS);
            }

            /* Normal polling interval */
            return (elapsed >= s->desc.poll_interval_ms);
        }
    }

    return false;
}

int modbus_slave_manager_get_runtime_state(uint8_t device_id, modbus_slave_runtime_state_t *out_state)
{
    if (out_state == NULL || device_id == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated && s_slaves[i].desc.device_id == device_id) {
            *out_state = s_slaves[i].runtime;
            return 0;
        }
    }

    return -ENOENT;
}

int modbus_slave_manager_export_config(void *buffer, size_t max_size, size_t *out_size)
{
    if (buffer == NULL || out_size == NULL) {
        return -EINVAL;
    }

    /* Count allocated entries */
    uint8_t count = 0;
    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated) count++;
    }

    size_t needed = sizeof(uint8_t) + (count * sizeof(modbus_slave_descriptor_t));
    if (max_size < needed) {
        return -ENOMEM;
    }

    uint8_t *ptr = (uint8_t *)buffer;
    *ptr++ = count;

    for (size_t i = 0; i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        if (s_slaves[i].is_allocated) {
            memcpy(ptr, &s_slaves[i].desc, sizeof(modbus_slave_descriptor_t));
            ptr += sizeof(modbus_slave_descriptor_t);
        }
    }

    *out_size = needed;
    return 0;
}

int modbus_slave_manager_import_config(const void *buffer, size_t size)
{
    if (buffer == NULL || size < sizeof(uint8_t)) {
        return -EINVAL;
    }

    const uint8_t *ptr = (const uint8_t *)buffer;
    uint8_t count = *ptr++;

    size_t expected = sizeof(uint8_t) + (count * sizeof(modbus_slave_descriptor_t));
    if (size < expected) {
        return -EINVAL;
    }

    memset(s_slaves, 0, sizeof(s_slaves));

    for (uint8_t i = 0; i < count && i < MODBUS_MAX_TOTAL_SLAVES; i++) {
        modbus_slave_descriptor_t desc;
        memcpy(&desc, ptr, sizeof(modbus_slave_descriptor_t));
        ptr += sizeof(modbus_slave_descriptor_t);

        s_slaves[i].desc = desc;
        s_slaves[i].is_allocated = true;
        s_slaves[i].runtime.health = MODBUS_HEALTH_ONLINE;
    }

    return 0;
}
