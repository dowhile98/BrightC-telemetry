/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bring-up diagnostic for a Modbus RTU bus.
 *
 * Purpose: separate an addressing problem from a register-map problem on a
 * slave that answers nothing. Both look identical from the polling loop,
 * because both end as MODBUS_RESULT_TIMEOUT. This walk narrows the bus down
 * to the address that answers and the register window that address accepts.
 *
 * Compiled only when CONFIG_APP_MODBUS_BUS_PROBE is enabled.
 */

#ifndef APP_APPLICATION_DIAGNOSTICS_MODBUS_BUS_PROBE_H_
#define APP_APPLICATION_DIAGNOSTICS_MODBUS_BUS_PROBE_H_

#include <stdint.h>

#include "interfaces/i_modbus_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Walk the slave addresses and the energy-meter register window.
 *
 * Runs one transaction at a time on @p client and logs every outcome at INF
 * level. It must run while the bus is idle: the polling threads of the same
 * bus would interleave their frames with these.
 *
 * @param[in] client Client bound to the physical bus to probe.
 * @param[in] bus_id Bus identifier, for the log lines only.
 */
void modbus_bus_probe_run(const i_modbus_client_t *client, uint8_t bus_id);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_DIAGNOSTICS_MODBUS_BUS_PROBE_H_ */
