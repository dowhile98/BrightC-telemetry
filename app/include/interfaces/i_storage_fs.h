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
 * 1. V-TABLE
 *============================================================================*/

typedef struct {
    /* --- Volume Operations --- */
    bool (*is_card_present)(void *impl);
    int (*mount)(void *impl, const char *mount_pt);
    int (*unmount)(void *impl, const char *mount_pt);
    int (*statvfs)(void *impl, const char *path, storage_fs_volume_info_t *out_vol);

    /* --- File Operations --- */
    int (*open)(void *impl, void **out_file_handle, const char *path, uint32_t flags);
    int (*close)(void *impl, void *file_handle);
    int (*read)(void *impl, void *file_handle, void *buf, size_t len, size_t *out_read);
    int (*write)(void *impl, void *file_handle, const void *buf, size_t len, size_t *out_written);
    int (*sync)(void *impl, void *file_handle);
    int (*seek)(void *impl, void *file_handle, int64_t offset, int whence);
    int (*tell)(void *impl, void *file_handle, int64_t *out_offset);
    int (*truncate)(void *impl, void *file_handle, int64_t length);

    /* --- Directory & Path Operations --- */
    int (*mkdir)(void *impl, const char *path);
    int (*stat)(void *impl, const char *path, storage_fs_stat_t *out_stat);
    int (*opendir)(void *impl, void **out_dir_handle, const char *path);
    int (*readdir)(void *impl, void *dir_handle, storage_fs_dirent_t *out_entry);
    int (*closedir)(void *impl, void *dir_handle);
    int (*unlink)(void *impl, const char *path);
    int (*rename)(void *impl, const char *old_path, const char *new_path);
} i_storage_fs_api_t;

/*============================================================================*
 * 2. HANDLE (fat pointer passed around to consumers)
 *============================================================================*/

typedef struct {
    const i_storage_fs_api_t *api;
    void *impl;
} i_storage_fs_t;

/*============================================================================*
 * 3. VALIDATED WRAPPERS
 *============================================================================*/

static inline bool i_storage_fs_is_bound(const i_storage_fs_t *storage)
{
    return (storage != NULL &&
            storage->api != NULL &&
            storage->impl != NULL &&
            storage->api->is_card_present != NULL &&
            storage->api->mount != NULL &&
            storage->api->unmount != NULL &&
            storage->api->statvfs != NULL &&
            storage->api->open != NULL &&
            storage->api->close != NULL &&
            storage->api->read != NULL &&
            storage->api->write != NULL &&
            storage->api->sync != NULL &&
            storage->api->seek != NULL &&
            storage->api->tell != NULL &&
            storage->api->mkdir != NULL &&
            storage->api->stat != NULL &&
            storage->api->opendir != NULL &&
            storage->api->readdir != NULL &&
            storage->api->closedir != NULL &&
            storage->api->unlink != NULL);
}

/* --- Volume Wrappers --- */

static inline bool i_storage_fs_is_card_present(const i_storage_fs_t *storage)
{
    if (!i_storage_fs_is_bound(storage)) {
        return false;
    }
    return storage->api->is_card_present(storage->impl);
}

static inline int i_storage_fs_mount(const i_storage_fs_t *storage, const char *mount_pt)
{
    if (!i_storage_fs_is_bound(storage) || mount_pt == NULL) {
        return -EINVAL;
    }
    return storage->api->mount(storage->impl, mount_pt);
}

static inline int i_storage_fs_unmount(const i_storage_fs_t *storage, const char *mount_pt)
{
    if (!i_storage_fs_is_bound(storage) || mount_pt == NULL) {
        return -EINVAL;
    }
    return storage->api->unmount(storage->impl, mount_pt);
}

static inline int i_storage_fs_statvfs(const i_storage_fs_t *storage,
                                       const char *path,
                                       storage_fs_volume_info_t *out_vol)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL || out_vol == NULL) {
        return -EINVAL;
    }
    return storage->api->statvfs(storage->impl, path, out_vol);
}

/* --- File Wrappers --- */

static inline int i_storage_fs_open(const i_storage_fs_t *storage,
                                    void **out_file_handle,
                                    const char *path,
                                    uint32_t flags)
{
    if (!i_storage_fs_is_bound(storage) || out_file_handle == NULL || path == NULL) {
        return -EINVAL;
    }
    return storage->api->open(storage->impl, out_file_handle, path, flags);
}

static inline int i_storage_fs_close(const i_storage_fs_t *storage, void *file_handle)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL) {
        return -EINVAL;
    }
    return storage->api->close(storage->impl, file_handle);
}

static inline int i_storage_fs_read(const i_storage_fs_t *storage,
                                    void *file_handle,
                                    void *buf,
                                    size_t len,
                                    size_t *out_read)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }
    return storage->api->read(storage->impl, file_handle, buf, len, out_read);
}

static inline int i_storage_fs_write(const i_storage_fs_t *storage,
                                     void *file_handle,
                                     const void *buf,
                                     size_t len,
                                     size_t *out_written)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }
    return storage->api->write(storage->impl, file_handle, buf, len, out_written);
}

static inline int i_storage_fs_sync(const i_storage_fs_t *storage, void *file_handle)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL) {
        return -EINVAL;
    }
    return storage->api->sync(storage->impl, file_handle);
}

static inline int i_storage_fs_seek(const i_storage_fs_t *storage,
                                    void *file_handle,
                                    int64_t offset,
                                    int whence)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL) {
        return -EINVAL;
    }
    return storage->api->seek(storage->impl, file_handle, offset, whence);
}

static inline int i_storage_fs_tell(const i_storage_fs_t *storage,
                                    void *file_handle,
                                    int64_t *out_offset)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL || out_offset == NULL) {
        return -EINVAL;
    }
    return storage->api->tell(storage->impl, file_handle, out_offset);
}

static inline int i_storage_fs_truncate(const i_storage_fs_t *storage,
                                        void *file_handle,
                                        int64_t length)
{
    if (!i_storage_fs_is_bound(storage) || file_handle == NULL || length < 0) {
        return -EINVAL;
    }
    if (storage->api->truncate == NULL) {
        return -ENOSYS;
    }
    return storage->api->truncate(storage->impl, file_handle, length);
}

/* --- Directory & Path Wrappers --- */

static inline int i_storage_fs_mkdir(const i_storage_fs_t *storage, const char *path)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL) {
        return -EINVAL;
    }
    return storage->api->mkdir(storage->impl, path);
}

static inline int i_storage_fs_stat(const i_storage_fs_t *storage,
                                    const char *path,
                                    storage_fs_stat_t *out_stat)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL || out_stat == NULL) {
        return -EINVAL;
    }
    return storage->api->stat(storage->impl, path, out_stat);
}

static inline int i_storage_fs_opendir(const i_storage_fs_t *storage,
                                       void **out_dir_handle,
                                       const char *path)
{
    if (!i_storage_fs_is_bound(storage) || out_dir_handle == NULL || path == NULL) {
        return -EINVAL;
    }
    return storage->api->opendir(storage->impl, out_dir_handle, path);
}

static inline int i_storage_fs_readdir(const i_storage_fs_t *storage,
                                       void *dir_handle,
                                       storage_fs_dirent_t *out_entry)
{
    if (!i_storage_fs_is_bound(storage) || dir_handle == NULL || out_entry == NULL) {
        return -EINVAL;
    }
    return storage->api->readdir(storage->impl, dir_handle, out_entry);
}

static inline int i_storage_fs_closedir(const i_storage_fs_t *storage, void *dir_handle)
{
    if (!i_storage_fs_is_bound(storage) || dir_handle == NULL) {
        return -EINVAL;
    }
    return storage->api->closedir(storage->impl, dir_handle);
}

static inline int i_storage_fs_unlink(const i_storage_fs_t *storage, const char *path)
{
    if (!i_storage_fs_is_bound(storage) || path == NULL) {
        return -EINVAL;
    }
    return storage->api->unlink(storage->impl, path);
}

static inline int i_storage_fs_rename(const i_storage_fs_t *storage,
                                      const char *old_path,
                                      const char *new_path)
{
    if (!i_storage_fs_is_bound(storage) || old_path == NULL || new_path == NULL) {
        return -EINVAL;
    }
    if (storage->api->rename == NULL) {
        return -ENOSYS;
    }
    return storage->api->rename(storage->impl, old_path, new_path);
}

/* Backward compatibility wrappers */
static inline int i_storage_fs_file_open(const i_storage_fs_t *storage,
                                         fs_file_t *out_file,
                                         const char *path,
                                         uint32_t flags)
{
    return i_storage_fs_open(storage, (void **)out_file, path, flags);
}

static inline int i_storage_fs_file_close(const i_storage_fs_t *storage, fs_file_t file)
{
    return i_storage_fs_close(storage, (void *)file);
}

static inline int i_storage_fs_file_read(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         void *buf,
                                         size_t count,
                                         size_t *out_bytes_read)
{
    return i_storage_fs_read(storage, (void *)file, buf, count, out_bytes_read);
}

static inline int i_storage_fs_file_write(const i_storage_fs_t *storage,
                                          fs_file_t file,
                                          const void *buf,
                                          size_t count,
                                          size_t *out_bytes_written)
{
    return i_storage_fs_write(storage, (void *)file, buf, count, out_bytes_written);
}

static inline int i_storage_fs_file_seek(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         int64_t offset,
                                         fs_seek_origin_t origin)
{
    return i_storage_fs_seek(storage, (void *)file, offset, (int)origin);
}

static inline int i_storage_fs_file_tell(const i_storage_fs_t *storage,
                                         fs_file_t file,
                                         int64_t *out_offset)
{
    return i_storage_fs_tell(storage, (void *)file, out_offset);
}

static inline int i_storage_fs_file_sync(const i_storage_fs_t *storage, fs_file_t file)
{
    return i_storage_fs_sync(storage, (void *)file);
}

static inline int i_storage_fs_file_truncate(const i_storage_fs_t *storage, fs_file_t file, int64_t length)
{
    return i_storage_fs_truncate(storage, (void *)file, length);
}

static inline int i_storage_fs_dir_open(const i_storage_fs_t *storage, fs_dir_t *out_dir, const char *path)
{
    return i_storage_fs_opendir(storage, (void **)out_dir, path);
}

static inline int i_storage_fs_dir_read(const i_storage_fs_t *storage, fs_dir_t dir, fs_dirent_t *out_entry)
{
    return i_storage_fs_readdir(storage, (void *)dir, out_entry);
}

static inline int i_storage_fs_dir_close(const i_storage_fs_t *storage, fs_dir_t dir)
{
    return i_storage_fs_closedir(storage, (void *)dir);
}

static inline int i_storage_fs_get_volume_info(const i_storage_fs_t *storage, fs_volume_info_t *out_info)
{
    return i_storage_fs_statvfs(storage, "/SD:", out_info);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_STORAGE_FS_H_ */
