/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ao_modbus.h"

#include <errno.h>

#include <zephyr/logging/log.h>

#include "domain/services/cwt_tm16pt_codec.h"
#include "domain/services/jsy_mk339_codec.h"

LOG_MODULE_REGISTER(ao_modbus, LOG_LEVEL_INF);

/** Perform one transaction for a claimed slave. */
static modbus_transaction_result_t poll_target(ao_modbus_t *self,
                                               const modbus_poll_target_t *target)
{
    uint16_t registers[JSY_MK339_ELECTRICAL_FULL_REG_COUNT];

    switch (target->dev_type) {
    case MODBUS_DEV_TYPE_CWT_TM16PT:
        /* The wrapper rejects an unbound client (reporting INVALID) instead of
         * waiting for a timeout that can never expire. */
        return i_modbus_client_read_holding_registers(
            self->client, target->slave_addr, CWT_TM16PT_REG_TEMP_SCALED_BASE,
            CWT_TM16PT_SCALED_REG_COUNT, registers, (int32_t) target->timeout_ms);

    case MODBUS_DEV_TYPE_JSY_MK339: {
        modbus_transaction_result_t res = i_modbus_client_read_holding_registers(
            self->client, target->slave_addr, JSY_MK339_REG_ELECTRICAL_BASE,
            JSY_MK339_ELECTRICAL_FULL_REG_COUNT, registers, (int32_t) target->timeout_ms);
        if (res == MODBUS_RESULT_OK) {
            energy_reading_t reading;
            if (jsy_mk339_parse_electrical_parameters(registers,
                                                      JSY_MK339_ELECTRICAL_FULL_REG_COUNT,
                                                      &reading) == 0) {
                reading.device_id = target->device_id;
                reading.timestamp_us = (uint64_t) k_ticks_to_us_near64(k_uptime_ticks());
                LOG_DBG("bus %u: dev %u P_total=%.2f kW, E_total=%.2f kWh",
                        self->bus_id, target->device_id,
                        (double)reading.total_active_power_kw,
                        reading.total_active_energy_kwh);
            }
        }
        return res;
    }

    default:
        LOG_WRN("bus %u: device %u has unsupported type %u", self->bus_id, target->device_id,
                target->dev_type);
        return MODBUS_RESULT_INVALID;
    }
}

/** Milliseconds to sleep before looking at the registry again. */
static uint32_t idle_wait_ms(ao_modbus_t *self, uint64_t now_ms)
{
    uint32_t next_due_ms = 0U;

    if (i_slave_registry_next_due_in(self->registry, self->bus_id, now_ms, &next_due_ms) == 0 &&
        next_due_ms > 0U && next_due_ms < AO_MODBUS_MAX_IDLE_WAIT_MS) {
        return next_due_ms;
    }

    return AO_MODBUS_MAX_IDLE_WAIT_MS;
}

static void ao_modbus_thread(void *p1, void *p2, void *p3)
{
    ao_modbus_t *self = (ao_modbus_t *) p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("bus %u: polling thread started", self->bus_id);

    while (self->is_running) {
        uint64_t now_ms = (uint64_t) k_uptime_get();
        modbus_poll_target_t target;

        if (i_slave_registry_claim_next_due(self->registry, self->bus_id, now_ms, &target) == 0) {
            modbus_transaction_result_t result = poll_target(self, &target);

            (void) i_slave_registry_record(self->registry, target.device_id, result,
                                           (uint64_t) k_uptime_get());
            continue;
        }

        modbus_event_t event;

        if (k_msgq_get(self->queue, &event, K_MSEC(idle_wait_ms(self, now_ms))) == 0 &&
            event.type == AO_MODBUS_EVENT_STOP) {
            break;
        }
    }

    LOG_INF("bus %u: polling thread stopped", self->bus_id);
}

int ao_modbus_init(ao_modbus_t *self, uint8_t bus_id, const i_slave_registry_t *registry,
                   const i_modbus_client_t *client, struct k_msgq *queue, k_thread_stack_t *stack,
                   size_t stack_size)
{
    if (self == NULL || registry == NULL || client == NULL || queue == NULL || stack == NULL ||
        stack_size == 0U) {
        return -EINVAL;
    }

    self->bus_id = bus_id;
    self->registry = registry;
    self->client = client;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;

    return 0;
}

int ao_modbus_start(ao_modbus_t *self)
{
    if (self == NULL || self->queue == NULL || self->stack == NULL) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    /* Drop commands left over from a previous run, a stale STOP above all. */
    k_msgq_purge(self->queue);

    self->is_running = true;

    (void) k_thread_create(&self->thread, self->stack, self->stack_size, ao_modbus_thread, self,
                           NULL, NULL, K_PRIO_PREEMPT(AO_MODBUS_PRIORITY), 0, K_NO_WAIT);
    (void) k_thread_name_set(&self->thread, (self->bus_id == (uint8_t) MODBUS_BUS_RS485_1)
                                                  ? "ao_modbus_1"
                                                  : "ao_modbus_2");

    return 0;
}

int ao_modbus_post(ao_modbus_t *self, const modbus_event_t *event)
{
    if (self == NULL || event == NULL || self->queue == NULL) {
        return -EINVAL;
    }

    if (k_msgq_put(self->queue, event, K_NO_WAIT) != 0) {
        return -ENOMSG;
    }

    return 0;
}

int ao_modbus_stop(ao_modbus_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    const modbus_event_t stop = {.type = AO_MODBUS_EVENT_STOP};

    /* The loop re-checks is_running, so a full queue still stops it. */
    self->is_running = false;
    (void) ao_modbus_post(self, &stop);

    /* Waiting here is what makes a later start() safe: recreating the thread
     * over a thread control block that is still in use is undefined. */
    int err = k_thread_join(&self->thread, K_MSEC(AO_MODBUS_STOP_WAIT_MS));

    if (err == -EDEADLK) {
        /* Never started: nothing to wait for. */
        return 0;
    }

    if (err != 0) {
        LOG_ERR("bus %u: polling thread did not stop (%d)", self->bus_id, err);
        return -EBUSY;
    }

    return 0;
}
