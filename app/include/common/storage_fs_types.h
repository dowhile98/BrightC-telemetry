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
    STORAGE_FS_OPEN_READ          = (1U << 0), /**< Open for read access */
    STORAGE_FS_OPEN_WRITE         = (1U << 1), /**< Open for write access */
    STORAGE_FS_OPEN_CREATE_ALWAYS = (1U << 2), /**< Create new file; if exists, truncate to 0 */
    STORAGE_FS_OPEN_CREATE_NEW    = (1U << 3), /**< Create file only if it does not already exist */
    STORAGE_FS_OPEN_APPEND        = (1U << 4), /**< Append writes to end of file */
} storage_fs_open_flags_t;

/* Backward-compatibility alias */
typedef storage_fs_open_flags_t fs_open_flags_t;
#define FS_OPEN_READ          STORAGE_FS_OPEN_READ
#define FS_OPEN_WRITE         STORAGE_FS_OPEN_WRITE
#define FS_OPEN_CREATE_ALWAYS STORAGE_FS_OPEN_CREATE_ALWAYS
#define FS_OPEN_CREATE_NEW    STORAGE_FS_OPEN_CREATE_NEW
#define FS_OPEN_APPEND        STORAGE_FS_OPEN_APPEND

/**
 * @brief Origin positions for file seek operations.
 */
typedef enum {
    STORAGE_FS_SEEK_SET = 0, /**< Seek relative to start of file */
    STORAGE_FS_SEEK_CUR = 1, /**< Seek relative to current file position */
    STORAGE_FS_SEEK_END = 2, /**< Seek relative to end of file */
} storage_fs_seek_origin_t;

/* Backward-compatibility alias.
 * Guarded: Zephyr's <zephyr/fs/fs.h> defines these to the same values (0/1/2),
 * and both headers can end up in the same translation unit (e.g. Modbus TCP
 * tests, whose sockets include chain pulls fs.h in). */
typedef storage_fs_seek_origin_t fs_seek_origin_t;
#ifndef FS_SEEK_SET
#define FS_SEEK_SET STORAGE_FS_SEEK_SET
#endif
#ifndef FS_SEEK_CUR
#define FS_SEEK_CUR STORAGE_FS_SEEK_CUR
#endif
#ifndef FS_SEEK_END
#define FS_SEEK_END STORAGE_FS_SEEK_END
#endif

/**
 * @brief Directory entry and file classification.
 */
typedef enum {
    STORAGE_FS_ENTRY_FILE = 0, /**< Regular data file */
    STORAGE_FS_ENTRY_DIR  = 1, /**< Directory node */
} storage_fs_entry_type_t;

/* Backward-compatibility alias */
typedef storage_fs_entry_type_t fs_entry_type_t;
#define FS_ENTRY_TYPE_FILE STORAGE_FS_ENTRY_FILE
#define FS_ENTRY_TYPE_DIR  STORAGE_FS_ENTRY_DIR

/*============================================================================*
 * 2. METADATA & VOLUME DESCRIPTIONS
 *============================================================================*/

/**
 * @brief File or directory status information.
 */
typedef struct {
    uint64_t                size_bytes;                 /**< File size in bytes (0 for directories) */
    storage_fs_entry_type_t type;                       /**< Entry classification (File vs Dir) */
    uint32_t                modified_timestamp;         /**< UNIX epoch timestamp if supported, else 0 */
    char                    name[FS_MAX_FILENAME_LEN];  /**< Base name of entry */
} storage_fs_stat_t;

/* Backward-compatibility alias */
typedef storage_fs_stat_t fs_stat_t;

/**
 * @brief Directory traversal entry.
 */
typedef struct {
    storage_fs_entry_type_t type;                       /**< Entry type */
    uint64_t                size_bytes;                 /**< File size in bytes */
    char                    name[FS_MAX_FILENAME_LEN];  /**< Entry name (null-terminated) */
} storage_fs_dirent_t;

/* Backward-compatibility alias */
typedef storage_fs_dirent_t fs_dirent_t;

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
} storage_fs_volume_info_t;

/* Backward-compatibility alias */
typedef storage_fs_volume_info_t fs_volume_info_t;

/*============================================================================*
 * 3. SD CARD HOT-PLUG LIFECYCLE & CONCURRENCY
 *============================================================================*/

/**
 * @brief High-level card status for hot-pluggable MicroSD cards.
 */
typedef enum {
    STORAGE_FS_CARD_UNKNOWN = 0,         /**< Unknown or uninitialized card status */
    STORAGE_FS_CARD_NOT_INSERTED,        /**< Card physically absent */
    STORAGE_FS_CARD_INSERTED_UNMOUNTED,  /**< Card inserted but unmounted / debouncing */
    STORAGE_FS_CARD_MOUNTED,             /**< Filesystem successfully mounted and operational */
    STORAGE_FS_CARD_EJECT_REQUESTED,     /**< Eject requested / draining active ops */
    STORAGE_FS_CARD_ERROR,               /**< Mount or hardware error */
} storage_fs_card_status_t;

/* Compatibility enum and mapping */
typedef enum {
    SD_CARD_STATE_NO_CARD = STORAGE_FS_CARD_NOT_INSERTED,
    SD_CARD_STATE_INSERTED_DEBOUNCING = STORAGE_FS_CARD_INSERTED_UNMOUNTED,
    SD_CARD_STATE_MOUNTING,
    SD_CARD_STATE_READY = STORAGE_FS_CARD_MOUNTED,
    SD_CARD_STATE_UNMOUNTING = STORAGE_FS_CARD_EJECT_REQUESTED,
    SD_CARD_STATE_ERROR = STORAGE_FS_CARD_ERROR,
    SD_CARD_STATE_MAX
} sd_card_state_t;

/**
 * @brief Asynchronous status notification published over Zbus.
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

struct fs_file_s;
typedef struct fs_file_s *fs_file_t;

struct fs_dir_s;
typedef struct fs_dir_s *fs_dir_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_STORAGE_FS_TYPES_H_ */
