/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bring-up diagnostic for a Modbus RTU bus. See modbus_bus_probe.h.
 *
 * The walk never stops at the first address that answers: more than one slave
 * shares a bus, and stopping early would report the healthy meter and hide the
 * silent one. Every address is logged, then each answering address is asked
 * for progressively wider register windows.
 */

#include "modbus_bus_probe.h"

#include <zephyr/logging/log.h>

#include "domain/services/cwt_tm16pt_codec.h"
#include "domain/services/jsy_mk339_codec.h"

LOG_MODULE_REGISTER(modbus_bus_probe, LOG_LEVEL_INF);

/** Address range walked when looking for a silent slave. */
#define PROBE_ADDR_FIRST 1U
#define PROBE_ADDR_LAST  4U

/** Addresses the walk can remember. One per address in the range above. */
#define PROBE_ADDR_SLOTS (PROBE_ADDR_LAST - PROBE_ADDR_FIRST + 1U)

/** Model register of the JSY-MK-339 family (expected 0x0339 when implemented). */
#define PROBE_MODEL_REG 0x0000U

/** First register of the JSY-MK-339 electrical block. */
#define PROBE_ELECTRICAL_REG JSY_MK339_REG_ELECTRICAL_BASE

/** Response timeout per attempt, matching the polling configuration. */
#define PROBE_TIMEOUT_MS 500U

/** Widest window this probe asks for, in words. */
#define PROBE_MAX_WORDS JSY_MK339_ELECTRICAL_FULL_REG_COUNT

/** Register windows tried once an address has answered. */
static const uint16_t k_quantities[] = {
	3U,                                       /* phase A: V, I, P */
	8U,                                       /* voltages + currents */
	16U,                                      /* .. active powers */
	JSY_MK339_ELECTRICAL_BASIC_REG_COUNT,     /* complete documented basic block (31) */
	40U,                                      /* .. reactive energies */
	JSY_MK339_ELECTRICAL_FULL_REG_COUNT,      /* full block (48) */
};

static const char *result_name(modbus_transaction_result_t result)
{
	switch (result) {
	case MODBUS_RESULT_OK:
		return "OK";
	case MODBUS_RESULT_TIMEOUT:
		return "TIMEOUT";
	case MODBUS_RESULT_CRC:
		return "CRC";
	case MODBUS_RESULT_EXCEPTION:
		return "EXCEPTION";
	case MODBUS_RESULT_INVALID:
	default:
		return "INVALID";
	}
}

/**
 * Tell whether anything on the wire answered.
 *
 * A Modbus exception is a frame from the addressed slave, so it proves the
 * address is right even though the register is not readable. A CRC error also
 * means bytes arrived. Only a timeout means the slave stayed silent.
 */
static bool responded(modbus_transaction_result_t result)
{
	return result != MODBUS_RESULT_TIMEOUT;
}

static void log_words(uint8_t bus_id, uint8_t addr, uint16_t first_reg, const uint16_t *regs,
		      uint16_t quantity)
{
	/* Eight words per line: enough to read the block, short enough that the
	 * deferred log buffer never truncates the line. */
	for (uint16_t offset = 0U; offset < quantity; offset += 8U) {
		uint16_t remaining = quantity - offset;
		uint16_t chunk = (remaining > 8U) ? 8U : remaining;

		if (chunk == 8U) {
			LOG_INF("probe: bus%u addr=%u reg=%04X+%u: %04X %04X %04X %04X %04X %04X %04X %04X",
				bus_id, addr, (unsigned int)(first_reg + offset), offset,
				regs[offset + 0U], regs[offset + 1U], regs[offset + 2U],
				regs[offset + 3U], regs[offset + 4U], regs[offset + 5U],
				regs[offset + 6U], regs[offset + 7U]);
		} else {
			LOG_INF("probe: bus%u addr=%u reg=%04X+%u: %04X %04X %04X %04X %04X %04X %04X",
				bus_id, addr, (unsigned int)(first_reg + offset), offset,
				regs[offset + 0U],
				(chunk > 1U) ? regs[offset + 1U] : 0U,
				(chunk > 2U) ? regs[offset + 2U] : 0U,
				(chunk > 3U) ? regs[offset + 3U] : 0U,
				(chunk > 4U) ? regs[offset + 4U] : 0U,
				(chunk > 5U) ? regs[offset + 5U] : 0U,
				(chunk > 6U) ? regs[offset + 6U] : 0U);
		}
	}
}

/** Read one register window and log it, with the words when it worked. */
static modbus_transaction_result_t read_and_log(const i_modbus_client_t *client, uint8_t bus_id,
					       uint8_t addr, uint16_t first_reg, uint16_t quantity,
					       uint16_t *regs)
{
	modbus_transaction_result_t result = i_modbus_client_read_holding_registers(
		client, addr, first_reg, quantity, regs, PROBE_TIMEOUT_MS);

	LOG_INF("probe: bus%u addr=%u base=%04X qty=%u -> %s", bus_id, addr, first_reg, quantity,
		result_name(result));

	if (result == MODBUS_RESULT_OK) {
		log_words(bus_id, addr, first_reg, regs, quantity);
	}

	return result;
}

void modbus_bus_probe_run(const i_modbus_client_t *client, uint8_t bus_id)
{
	if (!i_modbus_client_is_bound(client)) {
		LOG_ERR("probe: bus%u has no bound client", bus_id);
		return;
	}

	/* A local, not a static: the diagnostic must not add mutable global state.
	 * Sized for the widest window any phase asks for. */
	uint16_t regs[PROBE_MAX_WORDS];
	uint8_t answering[PROBE_ADDR_SLOTS];
	size_t answering_count = 0U;

	LOG_INF("probe: bus%u sweeping addresses %u..%u", bus_id, PROBE_ADDR_FIRST, PROBE_ADDR_LAST);

	for (uint8_t addr = PROBE_ADDR_FIRST; addr <= PROBE_ADDR_LAST; addr++) {
		k_msleep(20);
		modbus_transaction_result_t model = read_and_log(client, bus_id, addr, PROBE_MODEL_REG,
								 7U, regs);
		k_msleep(20);
		modbus_transaction_result_t electrical = read_and_log(
			client, bus_id, addr, PROBE_ELECTRICAL_REG, 3U, regs);

		if (responded(model) || responded(electrical)) {
			answering[answering_count] = addr;
			answering_count++;
		}
	}

	if (answering_count == 0U) {
		LOG_ERR("probe: bus%u no slave answered any address %u..%u", bus_id, PROBE_ADDR_FIRST,
			PROBE_ADDR_LAST);
		return;
	}

	for (size_t i = 0U; i < answering_count; i++) {
		uint8_t addr = answering[i];

		LOG_INF("probe: bus%u addressing addr=%u answered", bus_id, addr);

		/* Register window: how wide a block this address accepts. */
		for (size_t q = 0U; q < (sizeof(k_quantities) / sizeof(k_quantities[0])); q++) {
			k_msleep(20);
			(void)read_and_log(client, bus_id, addr, PROBE_ELECTRICAL_REG, k_quantities[q],
					   regs);
		}

		/* Temperature block: proves whether the healthy meter is decoded, not
		 * merely reached. Scaled mode is 0.1 C per count, signed. */
		k_msleep(20);
		(void)read_and_log(client, bus_id, addr, CWT_TM16PT_REG_TEMP_SCALED_BASE,
				   CWT_TM16PT_SCALED_REG_COUNT, regs);
	}

	LOG_INF("probe: bus%u done, %u address(es) answered", bus_id, (unsigned int)answering_count);
}
