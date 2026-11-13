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

/* Keep boot messages easy to scan when several modules are logging together. */
#define BOOT_INF(fmt, ...) LOG_INF("[BOOT] " fmt, ##__VA_ARGS__)
#define BOOT_WRN(fmt, ...) LOG_WRN("[BOOT] " fmt, ##__VA_ARGS__)
#define BOOT_ERR(fmt, ...) LOG_ERR("[BOOT] " fmt, ##__VA_ARGS__)

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
		BOOT_INF("factory-reset input asserted; sampling for 5 s");
		int held_ms = 0;
		while (held_ms < 5000 && gpio_pin_get_dt(&s_user_button) > 0) {
			k_sleep(K_MSEC(100));
			held_ms += 100;
		}

		if (held_ms >= 5000) {
			BOOT_WRN("factory-reset hold confirmed; restoring persistent defaults");
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
			BOOT_INF("factory-reset hold cancelled after %d ms; continuing normal boot", held_ms);
		}
	}
#else
	ARG_UNUSED(app);
#endif
}

int main(void)
{
	BOOT_INF("BrightC telemetry controller boot sequence started");
	BOOT_INF("initializing dependency-injection container and hardware adapters");
	int err = composition_root_init(&g_app);

	if (err != 0)
	{
		BOOT_ERR("composition-root initialization failed: rc=%d", err);
		return err;
	}
	BOOT_INF("composition-root initialization complete; all required dependencies wired");

	err = app_facade_init(&g_app);
	if (err != 0) {
		BOOT_ERR("application facade initialization failed: rc=%d", err);
		return err;
	}
	BOOT_INF("application facade initialized");

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
		BOOT_ERR("composition-root start failed: rc=%d", err);
		return err;
	}

	BOOT_INF("runtime started successfully; controller is operational");

	while (true)
	{
		k_sleep(K_MSEC(1000U));
	}
	return 0;
}
