/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Application entry point. The only mutable static object of the firmware is
 * the container below; every other component receives it by reference from the
 * composition root.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_APP_MODBUS_BUS_PROBE)
#include "application/diagnostics/modbus_bus_probe.h"
#endif

#include "infrastructure/di/composition_root.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static app_container_t g_app;

int main(void)
{
	int err = composition_root_init(&g_app);

	if (err != 0)
	{
		LOG_ERR("composition root init failed (%d)", err);
		return err;
	}

#if defined(CONFIG_APP_MODBUS_BUS_PROBE)
	/* Must run before composition_root_start(): the polling threads of this
	 * bus would interleave their frames with the probe's. */
	if (i_modbus_client_is_bound(&g_app.client[MODBUS_BUS_RS485_1]))
	{
		modbus_bus_probe_run(&g_app.client[MODBUS_BUS_RS485_1], (uint8_t) MODBUS_BUS_RS485_1);
	}
#endif

	err = composition_root_start(&g_app);
	if (err != 0)
	{
		LOG_ERR("composition root start failed (%d)", err);
		return err;
	}

	LOG_INF("BrightC telemetry controller started");

	while (true)
	{
		k_sleep(K_MSEC(1000U));
	}
	return 0;
}
