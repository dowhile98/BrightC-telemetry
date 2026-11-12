/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake filesystem storage port implementation for host & unit testing.
 * Zero Zephyr headers, zero hardware registers. Pure C99/C11.
 */

#ifndef TESTS_MOCKS_FAKE_STORAGE_FS_H_
#define TESTS_MOCKS_FAKE_STORAGE_FS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/storage_fs_types.h"
#include "interfaces/i_storage_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_STORAGE_FS_MAX_NODES        32U
#define FAKE_STORAGE_FS_FILE_MAX_SIZE    8192U
#define FAKE_STORAGE_FS_MAX_OPEN_FILES   8U
#define FAKE_STORAGE_FS_MAX_OPEN_DIRS    4U

typedef struct {
    char    path[FS_MAX_PATH_LEN];
    uint8_t data[FAKE_STORAGE_FS_FILE_MAX_SIZE];
    size_t  size;
    bool    exists;
    bool    is_directory;
} fake_storage_node_t;

typedef struct {
    uint32_t node_index;
    size_t   cursor;
    uint32_t flags;
    bool     is_active;
} fake_storage_file_handle_t;

typedef struct {
    char     dir_path[FS_MAX_PATH_LEN];
    uint32_t current_node_index;
    bool     is_active;
} fake_storage_dir_handle_t;

typedef struct {
    /* Physical presence and mount status */
    bool card_present;
    bool is_mounted;
    char mount_point[32];

    /* Error injection */
    int simulated_error;
    int simulated_mount_error;
    int simulated_io_error;

    /* Call invocation counters */
    uint32_t mount_calls;
    uint32_t unmount_calls;
    uint32_t open_calls;
    uint32_t close_calls;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t sync_calls;
    uint32_t seek_calls;
    uint32_t tell_calls;
    uint32_t mkdir_calls;
    uint32_t stat_calls;
    uint32_t statvfs_calls;
    uint32_t opendir_calls;
    uint32_t readdir_calls;
    uint32_t closedir_calls;
    uint32_t unlink_calls;

    /* Virtual memory node table */
    fake_storage_node_t nodes[FAKE_STORAGE_FS_MAX_NODES];
    fake_storage_file_handle_t file_handles[FAKE_STORAGE_FS_MAX_OPEN_FILES];
    fake_storage_dir_handle_t  dir_handles[FAKE_STORAGE_FS_MAX_OPEN_DIRS];
} fake_storage_fs_state_t;

/**
 * @brief Reset fake filesystem to an initial state (card present, unmounted, empty filesystem).
 */
void fake_storage_fs_reset(void);

/**
 * @brief Obtain an i_storage_fs_t port handle bound to the fake filesystem.
 */
i_storage_fs_t fake_storage_fs_get_port(void);

/**
 * @brief Alias for fake_storage_fs_get_port.
 */
i_storage_fs_t fake_storage_fs_get_handle(void);

/**
 * @brief Access direct state for test inspection and assertions.
 */
fake_storage_fs_state_t *fake_storage_fs_get_state(void);

/**
 * @brief Set card detect state directly.
 */
void fake_storage_fs_set_card_present(bool present);

/**
 * @brief Set global simulated error code for subsequent operations.
 */
void fake_storage_fs_set_error(int err);

/**
 * @brief Convenience helper: simulate physical card ejection.
 */
void fake_storage_fs_simulate_card_eject(void);

/**
 * @brief Convenience helper: simulate physical card insertion.
 */
void fake_storage_fs_simulate_card_insert(void);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_MOCKS_FAKE_STORAGE_FS_H_ */
