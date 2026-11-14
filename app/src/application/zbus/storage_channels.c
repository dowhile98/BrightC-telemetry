/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD storage subsystem Zbus channel definitions.
 */

#include "application/zbus/storage_channels.h"

ZBUS_CHAN_DEFINE(
    chan_storage_sd_status,
    sd_card_status_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_storage_sd_status_logger),
    ZBUS_MSG_INIT(
        .status = STORAGE_FS_CARD_NOT_INSERTED,
        .card_present = false,
        .is_mounted = false,
        .total_space_mb = 0,
        .free_space_mb = 0,
        .timestamp_ms = 0
    )
);
