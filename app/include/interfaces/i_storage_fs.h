/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port interface for filesystem storage (MicroSD FATFS / Removable media).
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_storage_fs_api_t)
 * 2) Handle (i_storage_fs_t)
 * 3) Validated wrappers (static inline i_storage_fs_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_STORAGE_FS_H_
#define APP_INTERFACES_I_STORAGE_FS_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/storage_fs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /* --- Volume Operations --- */
    int (*mount)(void *impl);
    int (*unmount)(void *impl);
    bool (*is_mounted)(void *impl);
    bool (*is_card_present)(void *impl);
    int (*get_volume_info)(void *impl, fs_volume_info_t *out_info);

    /* --- File Operations --- */
    int (*file_open)(void *impl, fs_file_t *out_file, const char *path, uint32_t flags);
    int (*file_close)(void *impl, fs_file_t file);
    int (*file_read)(void *impl, fs_file_t file, void *buf, size_t count, size_t *out_bytes_read);
    int (*file_write)(void *impl, fs_file_t file, const void *buf, size_t count, size_t *out_bytes_written);
    int (*file_seek)(void *impl, fs_file_t file, int64_t offset, fs_seek_origin_t origin);
    int (*file_tell)(void *impl, fs_file_t file, int64_t *out_offset);
    int (*file_sync)(void *impl, fs_file_t file);
    int (*file_truncate)(void *impl, fs_file_t file, int64_t length);

    /* --- Directory & Path Operations --- */
    int (*dir_open)(void *impl, fs_dir_t *out_dir, const char *path);
    int (*dir_read)(void *impl, fs_dir_t dir, fs_dirent_t *out_entry);
    int (*dir_close)(void *impl, fs_dir_t dir);
    int (*mkdir)(void *impl, const char *path);
    int (*unlink)(void *impl, const char *path);
    int (*rename)(void *impl, const char *old_path, const char *new_path);
    int (*stat)(void *impl, const char *path, fs_stat_t *out_stat);
} i_storage_fs_api_t;

/*============================================================================*
 * 2. HANDLE (fat pointer passed around to consumers)
 *============================================================================*/

typedef struct {
    const i_storage_fs_api_t *api;
    void *impl;
} i_storage_fs_t;

/*============================================================================*
 * 3. VALIDATED WRAPPERS (public interface used by application and use cases)
 *============================================================================*/

/**
 * @brief Check whether an i_storage_fs_t handle is bound to a valid implementation.
 *
 * @param storage Storage instance handle.
 * @return true if valid, false otherwise.
 */
static inline bool i_storage_fs_is_bound(const i_storage_fs_t *storage)
{
    return (storage != NULL &&
            storage->api != NULL &&
            storage->impl != NULL &&
            storage->api->mount != NULL &&
            storage->api->unmount != NULL &&
            storage->api->is_mounted != NULL &&
            storage->api->is_card_present != NULL &&
            storage->api->get_volume_info != NULL &&
            storage->api->file_open != NULL &&
            storage->api->file_close != NULL &&
            storage->api->file_read != NULL &&
            storage->api->file_write != NULL &&
            storage->api->file_seek != NULL &&
            storage->api->file_tell != NULL &&
            storage->api->file_sync != NULL &&
            storage->api->file_truncate != NULL &&
            storage->api->dir_open != NULL &&
            storage->api->dir_read != NULL &&
            storage->api->dir_close != NULL &&
            storage->api->mkdir != NULL &&
            storage->api->unlink != NULL &&
            storage->api->rename != NULL &&
            storage->api->stat != NULL);
}

/* --- Volume Wrappers --- */

static inline int i_storage_fs_mount(const i_storage_fs_t *storage)
{
    if (!i_storage_fs_is_bound(storage)) {
        return -EINVAL;
    }
    return storage->api->mount(storage->impl);
}

static inline int i_storage_fs_unmount(const i_storage_fs_t *storage)
{
    if (!i_storage_fs_is_bound(storage)) {
        return -EINVAL;
    }
    return storage->api->unmount(storage->impl);
}

static inline bool i_storage_fs_is_mounted(const i_storage_fs_t *storage)
{
    if (!i_storage_fs_is_bound(storage)) {
        return false;
    }
    return storage->api->is_mounted(storage->impl);
}

static inline bool i_storage_fs_is_card_present(const i_storage_fs_t *storage)
{
    if (!i_storage_fs_is_bound(storage)) {
        return false;
    }
    return storage->api->is_card_present(storage->impl);
}

static inline int i_storage_fs_get_volume_info(const i_storage_fs_t *storage, fs_volume_info_t *out_info)
{
    if (!i_storage_fs_is_bound(storage) || out_info == NULL) {
        return -EINVAL;
    }
    return storage->api->get_volume_info(storage->impl, out_info);
}

/* --- File Wrappers --- */

static inline int i_storage_fs_file_open(const i_storage_fs_t *storage,
                                         fs_file_t *out_file,
                                         const char *path,
                                         uint32_t flags)
{
    if (!i_storage_fs_is_bound(storage) || out_file == NULL || path == NULL) {
        return -EINVAL;
    }
    return storage->api->file_open(storage->impl, out_file, path, flags);
}

static inline int i_storage_fs_file_close(const i_storage_fs_t *storage, fs_file_t file)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL) {
        return -EINVAL;
    }
    return storage->api->file_close(storage->impl, file);
}

static inline int i_storage_fs_file_read(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         void *buf,
                                         size_t count,
                                         size_t *out_bytes_read)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL || buf == NULL) {
        return -EINVAL;
    }
    return storage->api->file_read(storage->impl, file, buf, count, out_bytes_read);
}

static inline int i_storage_fs_file_write(const i_storage_fs_t *storage,
                                          fs_file_t file,
                                          const void *buf,
                                          size_t count,
                                          size_t *out_bytes_written)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL || buf == NULL) {
        return -EINVAL;
    }
    return storage->api->file_write(storage->impl, file, buf, count, out_bytes_written);
}

static inline int i_storage_fs_file_seek(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         int64_t offset,
                                         fs_seek_origin_t origin)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL) {
        return -EINVAL;
    }
    return storage->api->file_seek(storage->impl, file, offset, origin);
}

static inline int i_storage_fs_file_tell(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         int64_t *out_offset)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL || out_offset == NULL) {
        return -EINVAL;
    }
    return storage->api->file_tell(storage->impl, file, out_offset);
}

static inline int i_storage_fs_file_sync(const i_storage_fs_t *storage, fs_file_t file)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL) {
        return -EINVAL;
    }
    return storage->api->file_sync(storage->impl, file);
}

static inline int i_storage_fs_file_truncate(const i_storage_fs_t *storage, fs_file_t file, int64_t length)
{
    if (!i_storage_fs_is_bound(storage) || file == NULL || length < 0) {
        return -EINVAL;
    }
    return storage->api->file_truncate(storage->impl, file, length);
}

/* --- Directory & Path Wrappers --- */

static inline int i_storage_fs_dir_open(const i_storage_fs_t *storage, fs_dir_t *out_dir, const char *path)
{
    if (!i_storage_fs_is_bound(storage) || out_dir == NULL || path == NULL) {
        return -EINVAL;
    }
    return storage->api->dir_open(storage->impl, out_dir, path);
}

static inline int i_storage_fs_dir_read(const i_storage_fs_t *storage, fs_dir_t dir, fs_dirent_t *out_entry)
{
    if (!i_storage_fs_is_bound(storage) || dir == NULL || out_entry == NULL) {
        return -EINVAL;
    }
    return storage->api->dir_read(storage->impl, dir, out_entry);
}

static inline int i_storage_fs_dir_close(const i_storage_fs_t *storage, fs_dir_t dir)
{
    if (!i_storage_fs_is_bound(storage) || dir == NULL) {
        return -EINVAL;
    }
    return storage->api->dir_close(storage->impl, dir);
}

static inline int i_storage_fs_mkdir(const i_storage_fs_t *storage, const char *path)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL) {
        return -EINVAL;
    }
    return storage->api->mkdir(storage->impl, path);
}

static inline int i_storage_fs_unlink(const i_storage_fs_t *storage, const char *path)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL) {
        return -EINVAL;
    }
    return storage->api->unlink(storage->impl, path);
}

static inline int i_storage_fs_rename(const i_storage_fs_t *storage, const char *old_path, const char *new_path)
{
    if (!i_storage_fs_is_bound(storage) || old_path == NULL || new_path == NULL) {
        return -EINVAL;
    }
    return storage->api->rename(storage->impl, old_path, new_path);
}

static inline int i_storage_fs_stat(const i_storage_fs_t *storage, const char *path, fs_stat_t *out_stat)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL || out_stat == NULL) {
        return -EINVAL;
    }
    return storage->api->stat(storage->impl, path, out_stat);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_STORAGE_FS_H_ */
