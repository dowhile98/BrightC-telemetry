/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Configuration and storage subsystem Zbus channels.
 */

#ifndef APP_APPLICATION_ZBUS_CONFIG_CHANNELS_H_
#define APP_APPLICATION_ZBUS_CONFIG_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include "common/config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Channel for system configuration change announcements and persistence notifications.
 */
ZBUS_CHAN_DECLARE(chan_system_config);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_CONFIG_CHANNELS_H_ */
