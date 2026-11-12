/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD storage subsystem Zbus channels.
 */

#ifndef APP_APPLICATION_ZBUS_STORAGE_CHANNELS_H_
#define APP_APPLICATION_ZBUS_STORAGE_CHANNELS_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include "common/storage_fs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    storage_fs_card_status_t status;
    bool                     card_present;
    bool                     is_mounted;
    uint32_t                 total_space_mb;
    uint32_t                 free_space_mb;
    uint32_t                 timestamp_ms;
} sd_card_status_msg_t;

ZBUS_CHAN_DECLARE(chan_storage_sd_status);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_STORAGE_CHANNELS_H_ */
