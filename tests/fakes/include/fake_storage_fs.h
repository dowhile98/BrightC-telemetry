/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake filesystem storage port implementation for host unit testing.
 * Provides virtual file/directory storage in RAM, call counters, fault injection,
 * and port handle factory.
 */

#ifndef FAKE_STORAGE_FS_H_
#define FAKE_STORAGE_FS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/storage_fs_types.h"
#include "interfaces/i_storage_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_FS_MAX_FILES        16U
#define FAKE_FS_FILE_MAX_SIZE    8192U
#define FAKE_FS_MAX_OPEN_HANDLES 8U

typedef struct {
    char name[FS_MAX_PATH_LEN];
    uint8_t data[FAKE_FS_FILE_MAX_SIZE];
    size_t size;
    bool exists;
    bool is_directory;
} fake_fs_node_t;

typedef struct {
    uint32_t node_index;
    size_t cursor;
    uint32_t flags;
    bool is_active;
} fake_fs_open_file_t;

typedef struct {
    /* Physical and state simulation */
    bool card_present;
    bool mounted;
    bool write_protected;

    /* Fault injection */
    int simulated_mount_error;
    int simulated_io_error;
    bool simulated_disk_full;

    /* Call counters */
    uint32_t mount_calls;
    uint32_t unmount_calls;
    uint32_t open_calls;
    uint32_t close_calls;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t sync_calls;

    /* File storage table */
    fake_fs_node_t nodes[FAKE_FS_MAX_FILES];
    fake_fs_open_file_t handles[FAKE_FS_MAX_OPEN_HANDLES];
} fake_storage_fs_state_t;

/**
 * @brief Reset the fake filesystem to an initial empty, card-present, mounted state.
 */
void fake_storage_fs_reset(void);

/**
 * @brief Obtain an i_storage_fs_t handle bound to the fake filesystem.
 */
i_storage_fs_t fake_storage_fs_get_handle(void);

/**
 * @brief Access direct state for test inspection and assertions.
 */
fake_storage_fs_state_t *fake_storage_fs_get_state(void);

/**
 * @brief Simulate physical card ejection.
 */
void fake_storage_fs_simulate_card_eject(void);

/**
 * @brief Simulate physical card insertion.
 */
void fake_storage_fs_simulate_card_insert(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_STORAGE_FS_H_ */
