/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file system_channels.h
 * @brief Zbus channel declarations for operational system events.
 */

#ifndef APP_APPLICATION_ZBUS_SYSTEM_CHANNELS_H_
#define APP_APPLICATION_ZBUS_SYSTEM_CHANNELS_H_

#include <zephyr/zbus/zbus.h>
#include "common/datalogger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zbus channel broadcasting operational system events.
 *
 * Payloads are system_event_msg_t structures.
 */
ZBUS_CHAN_DECLARE(chan_system_event);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_SYSTEM_CHANNELS_H_ */
