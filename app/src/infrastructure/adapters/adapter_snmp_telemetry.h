/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ADAPTER_SNMP_TELEMETRY_H_
#define ADAPTER_SNMP_TELEMETRY_H_

#include <zephyr/kernel.h>
#include <snmp/snmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_SNMP_AGENT)

/**
 * @brief Initialize and register BrightC temperature and energy MIB telemetry nodes.
 *
 * Registers:
 * 1. Process temperatures (supply, return, delta, room, outdoor, humidity)
 * 2. 3-phase AC electrical energy meter telemetry (voltages, currents, power, PF, kWh, Hz)
 * 3. Physical sensor channel table (CWT1 16 channels)
 *
 * @param[in,out] agent zephyr-snmp agent instance.
 * @return 0 on success, negative errno on failure.
 */
int adapter_snmp_telemetry_init(struct snmp_agent *agent);

#endif /* CONFIG_SNMP_AGENT */

#ifdef __cplusplus
}
#endif

#endif /* ADAPTER_SNMP_TELEMETRY_H_ */
