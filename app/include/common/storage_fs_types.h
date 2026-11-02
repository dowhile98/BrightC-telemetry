/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, event definitions and filesystem abstractions
 * for the MicroSD Storage & Filesystem subsystem on BrightC Controller V3.
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_COMMON_STORAGE_FS_TYPES_H_
#define APP_COMMON_STORAGE_FS_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length for file/path names */
#define FS_MAX_FILENAME_LEN     256
#define FS_MAX_PATH_LEN         512

/*============================================================================*
 * 1. FILE ACCESS FLAGS & ATTRIBUTES
 *============================================================================*/

/**
 * @brief Bitwise flags for file opening modes.
 */
typedef enum {
    FS_OPEN_READ          = (1U << 0), /**< Open for read access */
    FS_OPEN_WRITE         = (1U << 1), /**< Open for write access */
    FS_OPEN_CREATE_ALWAYS = (1U << 2), /**< Create new file; if exists, truncate to 0 */
    FS_OPEN_CREATE_NEW    = (1U << 3), /**< Create file only if it does not already exist */
    FS_OPEN_APPEND        = (1U << 4), /**< Append writes to end of file */
} fs_open_flags_t;

/**
 * @brief Origin positions for file seek operations.
 */
typedef enum {
    FS_SEEK_SET = 0, /**< Seek relative to start of file */
    FS_SEEK_CUR = 1, /**< Seek relative to current file position */
    FS_SEEK_END = 2, /**< Seek relative to end of file */
} fs_seek_origin_t;

/**
 * @brief Directory entry and file classification.
 */
typedef enum {
    FS_ENTRY_TYPE_FILE = 0, /**< Regular data file */
    FS_ENTRY_TYPE_DIR  = 1, /**< Directory node */
} fs_entry_type_t;

/*============================================================================*
 * 2. METADATA & VOLUME DESCRIPTIONS
 *============================================================================*/

/**
 * @brief File or directory status information.
 */
typedef struct {
    uint64_t        size_bytes;                 /**< File size in bytes (0 for directories) */
    fs_entry_type_t type;                       /**< Entry classification (File vs Dir) */
    uint32_t        modified_timestamp;         /**< UNIX epoch timestamp if supported, else 0 */
    char            name[FS_MAX_FILENAME_LEN];  /**< Base name of entry */
} fs_stat_t;

/**
 * @brief Directory traversal entry.
 */
typedef struct {
    fs_entry_type_t type;                       /**< Entry type */
    uint64_t        size_bytes;                 /**< File size in bytes */
    char            name[FS_MAX_FILENAME_LEN];  /**< Entry name (null-terminated) */
} fs_dirent_t;

/**
 * @brief Mounted filesystem volume statistics.
 */
typedef struct {
    uint64_t total_bytes;           /**< Total volume capacity in bytes */
    uint64_t free_bytes;            /**< Available free space in bytes */
    uint32_t sector_size_bytes;     /**< Physical disk sector size (e.g., 512) */
    uint32_t cluster_size_bytes;    /**< Allocation cluster size (e.g., 32768) */
    char     fs_type[16];           /**< Volume format (e.g., "FAT32", "exFAT") */
    char     volume_label[32];      /**< Volume label string */
} fs_volume_info_t;

/*============================================================================*
 * 3. SD CARD HOT-PLUG LIFECYCLE & CONCURRENCY
 *============================================================================*/

/**
 * @brief High-level lifecycle state machine for hot-pluggable MicroSD cards.
 */
typedef enum {
    SD_CARD_STATE_NO_CARD = 0,         /**< MicroSD physical card absent (PG5 HIGH) */
    SD_CARD_STATE_INSERTED_DEBOUNCING, /**< Card detect triggered, debouncing contact chatter */
    SD_CARD_STATE_MOUNTING,            /**< Initializing SDMMC1 peripheral and mounting FATFS */
    SD_CARD_STATE_READY,               /**< Filesystem successfully mounted and operational */
    SD_CARD_STATE_UNMOUNTING,          /**< Orderly unmount or surprise removal cleanup */
    SD_CARD_STATE_ERROR,               /**< Hardware failure, unsupported FS, or mount timeout */
    SD_CARD_STATE_MAX
} sd_card_state_t;

/**
 * @brief Asynchronous status notification published over Zbus (chan_storage_sd_status).
 */
typedef struct {
    sd_card_state_t state;               /**< Current state machine status */
    bool            is_mounted;          /**< True if filesystem is mounted and safe for I/O */
    bool            is_write_protected;  /**< Hardware write-protect active */
    int32_t         last_error;          /**< 0 on success, negative errno on failure */
    uint64_t        total_bytes;         /**< Total capacity in bytes (0 if unmounted) */
    uint64_t        free_bytes;          /**< Free capacity in bytes (0 if unmounted) */
} sd_status_event_t;

/*============================================================================*
 * 4. OPAQUE FILE & DIRECTORY HANDLE TYPES
 *============================================================================*/

/**
 * @brief Opaque forward declarations for implementation handles.
 */
struct fs_file_s;
typedef struct fs_file_s *fs_file_t;

struct fs_dir_s;
typedef struct fs_dir_s *fs_dir_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_STORAGE_FS_TYPES_H_ */
