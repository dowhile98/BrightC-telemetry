/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ao_modbus.h"

#include <errno.h>

#include <zephyr/logging/log.h>

#include "domain/services/cwt_tm16pt_codec.h"
#include "domain/services/jsy_mk339_codec.h"
#include "domain/services/waveshare_relay_codec.h"
#include "domain/services/mbsl32di_codec.h"

LOG_MODULE_REGISTER(ao_modbus, LOG_LEVEL_INF);

/** Perform one transaction for a claimed slave. */
static modbus_transaction_result_t poll_target(ao_modbus_t *self,
                                               const modbus_poll_target_t *target)
{
    uint16_t registers[JSY_MK339_ELECTRICAL_FULL_REG_COUNT];

    switch (target->dev_type)
    {
    case MODBUS_DEV_TYPE_CWT_TM16PT:
    {
        modbus_transaction_result_t res = i_modbus_client_read_holding_registers(
            self->client, target->slave_addr, CWT_TM16PT_REG_TEMP_SCALED_BASE,
            CWT_TM16PT_SCALED_REG_COUNT, registers, (int32_t)target->timeout_ms);
        if (res == MODBUS_RESULT_OK)
        {
            temperature_bank_16ch_t bank;
            if (cwt_tm16pt_parse_scaled_temperatures(registers, CWT_TM16PT_SCALED_REG_COUNT, &bank) == 0)
            {
                bank.device_id = target->device_id;
                bank.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
                bank.is_communication_valid = true;
                LOG_DBG("bus %u: dev %u CWT-TM16PT valid_mask=0x%04X T0=%.1f C",
                        self->bus_id, target->device_id, (unsigned int)bank.valid_channels_mask,
                        (double)bank.temperatures_c[0]);
                if (self->cwt_tm16pt_cb != NULL)
                {
                    self->cwt_tm16pt_cb(self->cwt_tm16pt_cb_ctx, target->device_id, &bank);
                }
            }
        }
        return res;
    }

    case MODBUS_DEV_TYPE_JSY_MK339:
    {
        modbus_transaction_result_t res = i_modbus_client_read_holding_registers(
            self->client, target->slave_addr, JSY_MK339_REG_ELECTRICAL_BASE,
            JSY_MK339_ELECTRICAL_FULL_REG_COUNT, registers, (int32_t)target->timeout_ms);
        if (res == MODBUS_RESULT_OK)
        {
            energy_reading_t reading;
            if (jsy_mk339_parse_electrical_parameters(registers,
                                                      JSY_MK339_ELECTRICAL_FULL_REG_COUNT,
                                                      &reading) == 0)
            {
                reading.device_id = target->device_id;
                reading.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
                LOG_DBG("bus %u: dev %u P_total=%.2f kW, E_total=%.2f kWh",
                        self->bus_id, target->device_id,
                        (double)reading.total_active_power_kw,
                        reading.total_active_energy_kwh);
                if (self->jsy_mk339_cb != NULL)
                {
                    self->jsy_mk339_cb(self->jsy_mk339_cb_ctx, target->device_id, &reading);
                }
            }
        }
        else
        {
            if (self->jsy_mk339_cb != NULL)
            {
                energy_reading_t invalid_reading;
                (void)memset(&invalid_reading, 0, sizeof(invalid_reading));
                invalid_reading.device_id = target->device_id;
                invalid_reading.is_valid = false;
                invalid_reading.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
                self->jsy_mk339_cb(self->jsy_mk339_cb_ctx, target->device_id, &invalid_reading);
            }
        }
        return res;
    }

    case MODBUS_DEV_TYPE_MBSL32DI:
    {
        modbus_transaction_result_t res = i_modbus_client_read_holding_registers(
            self->client, target->slave_addr, MBSL32DI_REG_INPUTS_BASE,
            MBSL32DI_INPUTS_REG_COUNT, registers, (int32_t)target->timeout_ms);
        if (res == MODBUS_RESULT_OK)
        {
            mbsl32di_reading_t reading;
            if (mbsl32di_parse_inputs(registers, MBSL32DI_INPUTS_REG_COUNT, &reading) == 0)
            {
                LOG_DBG("bus %u: dev %u MBSL32DI inputs=0x%08X", self->bus_id, target->device_id,
                        (unsigned int)reading.raw_inputs);
                if (self->mbsl32di_cb != NULL)
                {
                    self->mbsl32di_cb(self->mbsl32di_cb_ctx, target->device_id, reading.raw_inputs);
                }
            }
        }
        return res;
    }

    case MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH:
    {
        /* FC01: read all 32 coils at once (addresses 0x0000-0x001F).
         * The response is a 4-byte little-endian bitmask. */
        uint8_t coil_bytes[WAVESHARE_RELAY_COIL_BYTES];
        modbus_transaction_result_t res = i_modbus_client_read_coils(
            self->client, target->slave_addr, WAVESHARE_RELAY_COIL_ADDR_BASE,
            WAVESHARE_RELAY_32CH_CHANNEL_COUNT, coil_bytes, (int32_t)target->timeout_ms);
        if (res == MODBUS_RESULT_OK)
        {
            relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];
            if (waveshare_relay_parse_coils_response(coil_bytes,
                                                     WAVESHARE_RELAY_COIL_BYTES,
                                                     states) == 0)
            {
                LOG_DBG("bus %u: dev %u relay states parsed (%u channels)",
                        self->bus_id, target->device_id,
                        WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
                /* TODO: publish relay states on a Zbus channel when one exists. */
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
        next_due_ms > 0U && next_due_ms < AO_MODBUS_MAX_IDLE_WAIT_MS)
    {
        return next_due_ms;
    }

    return AO_MODBUS_MAX_IDLE_WAIT_MS;
}

static void ao_modbus_thread(void *p1, void *p2, void *p3)
{
    ao_modbus_t *self = (ao_modbus_t *)p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("bus %u: polling thread started", self->bus_id);

    bool should_terminate = false;
    while (!should_terminate)
    {
        modbus_event_t event;
        int err = k_msgq_get(self->queue, &event, K_FOREVER);
        if (err != 0)
        {
            continue;
        }
        if (event.type == AO_MODBUS_EVENT_TERMINATE)
        {
            break;
        }
        if (event.type != AO_MODBUS_EVENT_START)
        {
            continue;
        }

        LOG_INF("bus %u: polling thread entered inner loop", self->bus_id);

        while (self->is_running)
        {
            uint64_t now_ms = (uint64_t)k_uptime_get();
            modbus_poll_target_t target;

            if (i_slave_registry_claim_next_due(self->registry, self->bus_id, now_ms, &target) == 0)
            {
                modbus_transaction_result_t result = poll_target(self, &target);

                (void)i_slave_registry_record(self->registry, target.device_id, result,
                                              (uint64_t)k_uptime_get());

                if (result == MODBUS_RESULT_OK)
                {
                    self->consecutive_fails = 0U;
                    if (self->bus_fault_active)
                    {
                        self->bus_fault_active = false;
                        LOG_INF("bus %u: Modbus communication recovered", self->bus_id);
                        if (self->bus_fault_cb != NULL)
                        {
                            self->bus_fault_cb(self->bus_fault_cb_ctx, self->bus_id, false);
                        }
                    }
                }
                else
                {
                    LOG_WRN("bus %u: dev %u (addr %u) poll failed: %d",
                            self->bus_id, target.device_id, target.slave_addr, (int)result);
                    self->consecutive_fails++;
                    if (self->consecutive_fails >= MODBUS_CONSECUTIVE_FAILS_THRESHOLD && !self->bus_fault_active)
                    {
                        self->bus_fault_active = true;
                        LOG_ERR("bus %u: consecutive poll failures reached threshold (%u), signaling bus fault",
                                self->bus_id, self->consecutive_fails);
                        if (self->bus_fault_cb != NULL)
                        {
                            self->bus_fault_cb(self->bus_fault_cb_ctx, self->bus_id, true);
                        }
                    }
                }

                if (AO_MODBUS_INTER_POLL_DELAY_MS > 0U)
                {
                    k_msleep(AO_MODBUS_INTER_POLL_DELAY_MS);
                }
                continue;
            }

            if (k_msgq_get(self->queue, &event, K_MSEC(idle_wait_ms(self, now_ms))) == 0)
            {
                if (event.type == AO_MODBUS_EVENT_STOP)
                {
                    break;
                }
                if (event.type == AO_MODBUS_EVENT_TERMINATE)
                {
                    should_terminate = true;
                    break;
                }

                if (event.type == AO_MODBUS_EVENT_WRITE_COIL)
                {
                    /* Asynchronous relay write: half-duplex bus is owned by this
                     * thread, so no other context may transmit on the UART. */
                    modbus_transaction_result_t res = i_modbus_client_write_single_coil(
                        self->client, event.slave_addr, event.coil_addr, event.coil_value,
                        event.timeout_ms);

                    (void)i_slave_registry_record(self->registry, event.device_id, res,
                                                  (uint64_t)k_uptime_get());

                    LOG_DBG("bus %u: write coil dev=%u addr=0x%04X value=0x%04X -> %d",
                            self->bus_id, event.device_id, event.coil_addr, event.coil_value, res);

                    if (AO_MODBUS_INTER_POLL_DELAY_MS > 0U)
                    {
                        k_msleep(AO_MODBUS_INTER_POLL_DELAY_MS);
                    }
                }

                continue;
            }
        }

        self->is_running = false;
        LOG_INF("bus %u: polling thread exited inner loop", self->bus_id);
        if (!should_terminate)
        {
            (void)k_sem_give(&self->stopped_sem);
        }
    }

    LOG_INF("bus %u: polling thread terminated", self->bus_id);
    (void)k_sem_give(&self->stopped_sem);
}

int ao_modbus_init(ao_modbus_t *self, uint8_t bus_id, const i_slave_registry_t *registry,
                   const i_modbus_client_t *client, struct k_msgq *queue, k_thread_stack_t *stack,
                   size_t stack_size)
{
    if (self == NULL || registry == NULL || client == NULL || queue == NULL || stack == NULL ||
        stack_size == 0U)
    {
        return -EINVAL;
    }

    self->bus_id = bus_id;
    self->registry = registry;
    self->client = client;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;
    self->mbsl32di_cb = NULL;
    self->mbsl32di_cb_ctx = NULL;
    self->cwt_tm16pt_cb = NULL;
    self->cwt_tm16pt_cb_ctx = NULL;
    self->bus_fault_cb = NULL;
    self->bus_fault_cb_ctx = NULL;
    self->consecutive_fails = 0U;
    self->bus_fault_active = false;

    k_sem_init(&self->stopped_sem, 0, 1);
    self->is_initialized = true;

    (void)k_thread_create(&self->thread, self->stack, self->stack_size, ao_modbus_thread, self,
                          NULL, NULL, K_PRIO_PREEMPT(AO_MODBUS_PRIORITY), 0, K_NO_WAIT);
    (void)k_thread_name_set(&self->thread, (self->bus_id == (uint8_t)MODBUS_BUS_RS485_1)
                                               ? "ao_modbus_1"
                                               : "ao_modbus_2");

    return 0;
}

int ao_modbus_start(ao_modbus_t *self)
{
    if (self == NULL || !self->is_initialized)
    {
        return -EINVAL;
    }

    if (self->is_running)
    {
        return -EALREADY;
    }

    /* Drop commands left over from a previous run, a stale STOP above all. */
    k_msgq_purge(self->queue);

    self->is_running = true;

    modbus_event_t evt = {
        .type = AO_MODBUS_EVENT_START,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_modbus_post(ao_modbus_t *self, const modbus_event_t *event)
{
    if (self == NULL || event == NULL || self->queue == NULL)
    {
        return -EINVAL;
    }

    if (k_msgq_put(self->queue, event, K_NO_WAIT) != 0)
    {
        return -ENOMSG;
    }

    return 0;
}

int ao_modbus_stop(ao_modbus_t *self)
{
    if (self == NULL || !self->is_initialized)
    {
        return -EINVAL;
    }

    if (!self->is_running)
    {
        return 0;
    }

    const modbus_event_t stop = {.type = AO_MODBUS_EVENT_STOP};
    self->is_running = false;
    return ao_modbus_post(self, &stop);
}

int ao_modbus_wait_stopped(ao_modbus_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized)
    {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_modbus_deinit(ao_modbus_t *self)
{
    if (self == NULL || !self->is_initialized)
    {
        return -EINVAL;
    }
    if (self->is_running)
    {
        return -EBUSY;
    }

    const modbus_event_t term = {.type = AO_MODBUS_EVENT_TERMINATE};
    (void)k_msgq_put(self->queue, &term, K_NO_WAIT);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0)
    {
        LOG_WRN("bus %u: ao_modbus deinit timeout, forcing abort", self->bus_id);
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}

void ao_modbus_set_mbsl32di_callback(ao_modbus_t *self, ao_modbus_mbsl32di_cb_t cb, void *ctx)
{
    if (self != NULL)
    {
        self->mbsl32di_cb = cb;
        self->mbsl32di_cb_ctx = ctx;
    }
}

void ao_modbus_set_cwt_tm16pt_callback(ao_modbus_t *self, ao_modbus_cwt_tm16pt_cb_t cb, void *ctx)
{
    if (self != NULL)
    {
        self->cwt_tm16pt_cb = cb;
        self->cwt_tm16pt_cb_ctx = ctx;
    }
}

void ao_modbus_set_jsy_mk339_callback(ao_modbus_t *self, ao_modbus_jsy_mk339_cb_t cb, void *ctx)
{
    if (self != NULL)
    {
        self->jsy_mk339_cb = cb;
        self->jsy_mk339_cb_ctx = ctx;
    }
}

void ao_modbus_set_bus_fault_callback(ao_modbus_t *self, ao_modbus_bus_fault_cb_t cb, void *ctx)
{
    if (self != NULL)
    {
        self->bus_fault_cb = cb;
        self->bus_fault_cb_ctx = ctx;
    }
}
