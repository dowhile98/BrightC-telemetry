/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Application entry point. The only mutable static object of the firmware is
 * the container below; every other component receives it by reference from the
 * composition root.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_APP_MODBUS_BUS_PROBE)
#include "application/diagnostics/modbus_bus_probe.h"
#endif

#include "application/services/app_facade.h"
#include "infrastructure/di/composition_root.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static app_container_t g_app;

#if DT_NODE_EXISTS(DT_ALIAS(sw0))
static const struct gpio_dt_spec s_user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif

static void check_boot_factory_reset(app_container_t *app)
{
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
	if (!gpio_is_ready_dt(&s_user_button)) {
		return;
	}

	if (gpio_pin_configure_dt(&s_user_button, GPIO_INPUT) != 0) {
		return;
	}

	if (gpio_pin_get_dt(&s_user_button) > 0) {
		LOG_INF("Factory reset button held at boot, waiting 5 seconds...");
		int held_ms = 0;
		while (held_ms < 5000 && gpio_pin_get_dt(&s_user_button) > 0) {
			k_sleep(K_MSEC(100));
			held_ms += 100;
		}

		if (held_ms >= 5000) {
			LOG_WRN("Button held > 5 seconds: Performing factory reset!");
			(void)app_facade_storage_reset_defaults();

			const i_led_t *led = app_container_get_led(app);
			if (led != NULL) {
				for (int i = 0; i < 5; i++) {
					(void)i_led_set_state(led, true);
					k_sleep(K_MSEC(100));
					(void)i_led_set_state(led, false);
					k_sleep(K_MSEC(100));
				}
			}
		} else {
			LOG_INF("Button released early, continuing standard boot");
		}
	}
#else
	ARG_UNUSED(app);
#endif
}

int main(void)
{
	int err = composition_root_init(&g_app);

	if (err != 0)
	{
		LOG_ERR("composition root init failed (%d)", err);
		return err;
	}

	(void)app_facade_init(&g_app);

	check_boot_factory_reset(&g_app);

#if defined(CONFIG_APP_MODBUS_BUS_PROBE)
	/* Must run before composition_root_start(): the polling threads of this
	 * bus would interleave their frames with the probe's. */
	const i_modbus_client_t *probe_client =
		app_container_get_modbus_client(&g_app, (uint8_t)MODBUS_BUS_RS485_1);

	if (probe_client != NULL && i_modbus_client_is_bound(probe_client))
	{
		modbus_bus_probe_run(probe_client, (uint8_t)MODBUS_BUS_RS485_1);
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
