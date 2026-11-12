/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Storage and FATFS Zephyr Infrastructure Adapter.
 * Binds Zephyr VFS (<zephyr/fs/fs.h>) and Disk Access (<zephyr/storage/disk_access.h>)
 * to the abstract i_storage_fs_t port interface.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_SD_ZEPHYR_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_SD_ZEPHYR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <ff.h>
#endif

#include "common/storage_fs_types.h"
#include "interfaces/i_storage_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADAPTER_STORAGE_SD_MAX_OPEN_FILES 8U
#define ADAPTER_STORAGE_SD_MAX_OPEN_DIRS  4U
#define ADAPTER_STORAGE_SD_DEFAULT_MOUNT_POINT "/SD:"
#define ADAPTER_STORAGE_SD_DEFAULT_DISK_NAME "SD"

typedef struct {
    struct fs_file_t file;
    bool in_use;
} adapter_sd_file_slot_t;

typedef struct {
    struct fs_dir_t dir;
    bool in_use;
} adapter_sd_dir_slot_t;

typedef struct {
    struct fs_mount_t mp;
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
    FATFS fat_fs;
#endif
    atomic_t in_flight_ops;
    atomic_t is_mounted;
    char mount_point[32];
    char disk_name[16];
    adapter_sd_file_slot_t file_slots[ADAPTER_STORAGE_SD_MAX_OPEN_FILES];
    adapter_sd_dir_slot_t  dir_slots[ADAPTER_STORAGE_SD_MAX_OPEN_DIRS];
    struct k_mutex lock;
    bool is_initialized;
} adapter_storage_sd_zephyr_t;

/**
 * @brief Initialize the Zephyr MicroSD storage adapter.
 *
 * @param[in,out] self        Adapter instance.
 * @param[in]     disk_name   Disk name (default "SD" if NULL).
 * @param[in]     mount_point Mount point path (default "/SD:" if NULL).
 * @return 0 on success, negative errno on failure.
 */
int adapter_storage_sd_zephyr_init(adapter_storage_sd_zephyr_t *self,
                                   const char *disk_name,
                                   const char *mount_point);

/**
 * @brief Obtain an abstract i_storage_fs_t port handle bound to this adapter.
 */
i_storage_fs_t adapter_storage_sd_zephyr_bind(adapter_storage_sd_zephyr_t *self);

/**
 * @brief Wait for in-flight I/O operations to drain.
 *
 * @param[in] self    Adapter instance.
 * @param[in] timeout Timeout to wait.
 * @return 0 if drained, -ETIMEDOUT on timeout.
 */
int adapter_storage_sd_zephyr_drain_in_flight(adapter_storage_sd_zephyr_t *self,
                                              k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_STORAGE_SD_ZEPHYR_H_ */
