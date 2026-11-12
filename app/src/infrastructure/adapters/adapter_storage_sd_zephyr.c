/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroSD Storage and FATFS Zephyr Infrastructure Adapter Implementation.
 */

#include "infrastructure/adapters/adapter_storage_sd_zephyr.h"
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adapter_storage_sd, CONFIG_LOG_DEFAULT_LEVEL);

static bool adapter_is_card_present(void *impl)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL) {
        return false;
    }

    const char *disk = (self->disk_name[0] != '\0') ? self->disk_name : ADAPTER_STORAGE_SD_DEFAULT_DISK_NAME;
    int status = disk_access_status(disk);
    if (status < 0) {
        return false;
    }

    return ((status & DISK_STATUS_NOMEDIA) == 0);
}

static int adapter_mount(void *impl, const char *mount_pt)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }

    if (!adapter_is_card_present(self)) {
        return -ENODEV;
    }

    if (atomic_get(&self->is_mounted)) {
        return 0;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    if (mount_pt != NULL) {
        strncpy(self->mount_point, mount_pt, sizeof(self->mount_point) - 1);
        self->mount_point[sizeof(self->mount_point) - 1] = '\0';
    }

    self->mp.type = FS_FATFS;
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
    self->mp.fs_data = &self->fat_fs;
#else
    self->mp.fs_data = NULL;
#endif
    self->mp.mnt_point = self->mount_point;

    int rc = fs_mount(&self->mp);
    if (rc == 0) {
        atomic_set(&self->is_mounted, 1);
        LOG_INF("MicroSD successfully mounted on %s", self->mount_point);
    } else {
        LOG_ERR("Failed to mount MicroSD on %s: %d", self->mount_point, rc);
    }

    k_mutex_unlock(&self->lock);
    return rc;
}

static int adapter_unmount(void *impl, const char *mount_pt)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    (void)mount_pt;

    if (!atomic_get(&self->is_mounted)) {
        return 0;
    }

    /* Mark as unmounted so new I/O is rejected immediately */
    atomic_set(&self->is_mounted, 0);

    /* Drain pending operations with a 100 ms timeout */
    adapter_storage_sd_zephyr_drain_in_flight(self, K_MSEC(100));

    k_mutex_lock(&self->lock, K_FOREVER);

    /* Close any lingering open file handles */
    for (size_t i = 0; i < ADAPTER_STORAGE_SD_MAX_OPEN_FILES; i++) {
        if (self->file_slots[i].in_use) {
            fs_close(&self->file_slots[i].file);
            self->file_slots[i].in_use = false;
        }
    }

    /* Close any lingering directory handles */
    for (size_t i = 0; i < ADAPTER_STORAGE_SD_MAX_OPEN_DIRS; i++) {
        if (self->dir_slots[i].in_use) {
            fs_closedir(&self->dir_slots[i].dir);
            self->dir_slots[i].in_use = false;
        }
    }

    int rc = fs_unmount(&self->mp);
    if (rc != 0) {
        LOG_WRN("MicroSD unmount returned %d", rc);
    } else {
        LOG_INF("MicroSD unmounted successfully from %s", self->mount_point);
    }

    k_mutex_unlock(&self->lock);
    return rc;
}

static int adapter_statvfs(void *impl, const char *path, storage_fs_volume_info_t *out_vol)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || out_vol == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    atomic_inc(&self->in_flight_ops);

    const char *p = (path != NULL) ? path : self->mount_point;
    struct fs_statvfs stat;
    int rc = fs_statvfs(p, &stat);

    if (rc == 0) {
        out_vol->sector_size_bytes = (uint32_t)stat.f_bsize;
        out_vol->cluster_size_bytes = (uint32_t)stat.f_frsize;
        out_vol->total_bytes = (uint64_t)stat.f_blocks * stat.f_frsize;
        out_vol->free_bytes = (uint64_t)stat.f_bfree * stat.f_frsize;
        strncpy(out_vol->fs_type, "FATFS", sizeof(out_vol->fs_type) - 1);
        out_vol->fs_type[sizeof(out_vol->fs_type) - 1] = '\0';
        strncpy(out_vol->volume_label, "BRIGHTC", sizeof(out_vol->volume_label) - 1);
        out_vol->volume_label[sizeof(out_vol->volume_label) - 1] = '\0';
    }

    atomic_dec(&self->in_flight_ops);
    return rc;
}

static int adapter_open(void *impl, void **out_file_handle, const char *path, uint32_t flags)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || out_file_handle == NULL || path == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    fs_mode_t zflags = 0;
    if (flags & STORAGE_FS_OPEN_READ) {
        zflags |= FS_O_READ;
    }
    if (flags & STORAGE_FS_OPEN_WRITE) {
        zflags |= FS_O_WRITE;
    }
    if (flags & STORAGE_FS_OPEN_CREATE_ALWAYS) {
        zflags |= (FS_O_CREATE | FS_O_WRITE);
    }
    if (flags & STORAGE_FS_OPEN_CREATE_NEW) {
        zflags |= (FS_O_CREATE | FS_O_WRITE);
    }
    if (flags & STORAGE_FS_OPEN_APPEND) {
        zflags |= FS_O_APPEND;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    adapter_sd_file_slot_t *slot = NULL;
    for (size_t i = 0; i < ADAPTER_STORAGE_SD_MAX_OPEN_FILES; i++) {
        if (!self->file_slots[i].in_use) {
            slot = &self->file_slots[i];
            break;
        }
    }

    if (slot == NULL) {
        k_mutex_unlock(&self->lock);
        return -EMFILE;
    }

    fs_file_t_init(&slot->file);
    atomic_inc(&self->in_flight_ops);

    int rc = fs_open(&slot->file, path, zflags);
    if (rc == 0) {
        slot->in_use = true;
        *out_file_handle = slot;
    } else {
        atomic_dec(&self->in_flight_ops);
    }

    k_mutex_unlock(&self->lock);
    return rc;
}

static int adapter_close(void *impl, void *file_handle)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL) {
        return -EINVAL;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;

    k_mutex_lock(&self->lock, K_FOREVER);
    int rc = 0;
    if (slot->in_use) {
        rc = fs_close(&slot->file);
        slot->in_use = false;
        atomic_dec(&self->in_flight_ops);
    }
    k_mutex_unlock(&self->lock);

    return rc;
}

static int adapter_read(void *impl, void *file_handle, void *buf, size_t len, size_t *out_read)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);

    ssize_t rc = fs_read(&slot->file, buf, len);
    if (rc >= 0) {
        if (out_read) {
            *out_read = (size_t)rc;
        }
        rc = 0;
    }

    atomic_dec(&self->in_flight_ops);
    return (int)rc;
}

static int adapter_write(void *impl, void *file_handle, const void *buf, size_t len, size_t *out_written)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);

    ssize_t rc = fs_write(&slot->file, buf, len);
    if (rc >= 0) {
        if (out_written) {
            *out_written = (size_t)rc;
        }
        rc = 0;
    }

    atomic_dec(&self->in_flight_ops);
    return (int)rc;
}

static int adapter_sync(void *impl, void *file_handle)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_sync(&slot->file);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static int adapter_seek(void *impl, void *file_handle, int64_t offset, int whence)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    int zwhence = FS_SEEK_SET;
    if (whence == STORAGE_FS_SEEK_CUR) {
        zwhence = FS_SEEK_CUR;
    } else if (whence == STORAGE_FS_SEEK_END) {
        zwhence = FS_SEEK_END;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_seek(&slot->file, (off_t)offset, zwhence);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static int adapter_tell(void *impl, void *file_handle, int64_t *out_offset)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL || out_offset == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);
    off_t pos = fs_tell(&slot->file);
    atomic_dec(&self->in_flight_ops);

    if (pos < 0) {
        return (int)pos;
    }

    *out_offset = (int64_t)pos;
    return 0;
}

static int adapter_truncate(void *impl, void *file_handle, int64_t length)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || file_handle == NULL || length < 0) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_file_slot_t *slot = (adapter_sd_file_slot_t *)file_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_truncate(&slot->file, (off_t)length);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static int adapter_mkdir(void *impl, const char *path)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || path == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_mkdir(path);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static int adapter_stat(void *impl, const char *path, storage_fs_stat_t *out_stat)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || path == NULL || out_stat == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    atomic_inc(&self->in_flight_ops);
    struct fs_dirent zdent;
    int rc = fs_stat(path, &zdent);

    if (rc == 0) {
        out_stat->size_bytes = zdent.size;
        out_stat->type = (zdent.type == FS_DIR_ENTRY_DIR) ? STORAGE_FS_ENTRY_DIR : STORAGE_FS_ENTRY_FILE;
        out_stat->modified_timestamp = 0;
        strncpy(out_stat->name, zdent.name, sizeof(out_stat->name) - 1);
        out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    }

    atomic_dec(&self->in_flight_ops);
    return rc;
}

static int adapter_opendir(void *impl, void **out_dir_handle, const char *path)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || out_dir_handle == NULL || path == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    adapter_sd_dir_slot_t *slot = NULL;
    for (size_t i = 0; i < ADAPTER_STORAGE_SD_MAX_OPEN_DIRS; i++) {
        if (!self->dir_slots[i].in_use) {
            slot = &self->dir_slots[i];
            break;
        }
    }

    if (slot == NULL) {
        k_mutex_unlock(&self->lock);
        return -EMFILE;
    }

    fs_dir_t_init(&slot->dir);
    atomic_inc(&self->in_flight_ops);

    int rc = fs_opendir(&slot->dir, path);
    if (rc == 0) {
        slot->in_use = true;
        *out_dir_handle = slot;
    } else {
        atomic_dec(&self->in_flight_ops);
    }

    k_mutex_unlock(&self->lock);
    return rc;
}

static int adapter_readdir(void *impl, void *dir_handle, storage_fs_dirent_t *out_entry)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || dir_handle == NULL || out_entry == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    adapter_sd_dir_slot_t *slot = (adapter_sd_dir_slot_t *)dir_handle;
    if (!slot->in_use) {
        return -EBADF;
    }

    atomic_inc(&self->in_flight_ops);
    struct fs_dirent zdent;
    int rc = fs_readdir(&slot->dir, &zdent);

    if (rc == 0) {
        out_entry->size_bytes = zdent.size;
        out_entry->type = (zdent.type == FS_DIR_ENTRY_DIR) ? STORAGE_FS_ENTRY_DIR : STORAGE_FS_ENTRY_FILE;
        strncpy(out_entry->name, zdent.name, sizeof(out_entry->name) - 1);
        out_entry->name[sizeof(out_entry->name) - 1] = '\0';
    }

    atomic_dec(&self->in_flight_ops);
    return rc;
}

static int adapter_closedir(void *impl, void *dir_handle)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || dir_handle == NULL) {
        return -EINVAL;
    }

    adapter_sd_dir_slot_t *slot = (adapter_sd_dir_slot_t *)dir_handle;

    k_mutex_lock(&self->lock, K_FOREVER);
    int rc = 0;
    if (slot->in_use) {
        rc = fs_closedir(&slot->dir);
        slot->in_use = false;
        atomic_dec(&self->in_flight_ops);
    }
    k_mutex_unlock(&self->lock);

    return rc;
}

static int adapter_unlink(void *impl, const char *path)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || path == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_unlink(path);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static int adapter_rename(void *impl, const char *old_path, const char *new_path)
{
    adapter_storage_sd_zephyr_t *self = (adapter_storage_sd_zephyr_t *)impl;
    if (self == NULL || old_path == NULL || new_path == NULL) {
        return -EINVAL;
    }

    if (!atomic_get(&self->is_mounted)) {
        return -ENODEV;
    }

    atomic_inc(&self->in_flight_ops);
    int rc = fs_rename(old_path, new_path);
    atomic_dec(&self->in_flight_ops);

    return rc;
}

static const i_storage_fs_api_t g_adapter_storage_sd_api = {
    .is_card_present = adapter_is_card_present,
    .mount = adapter_mount,
    .unmount = adapter_unmount,
    .statvfs = adapter_statvfs,
    .open = adapter_open,
    .close = adapter_close,
    .read = adapter_read,
    .write = adapter_write,
    .sync = adapter_sync,
    .seek = adapter_seek,
    .tell = adapter_tell,
    .truncate = adapter_truncate,
    .mkdir = adapter_mkdir,
    .stat = adapter_stat,
    .opendir = adapter_opendir,
    .readdir = adapter_readdir,
    .closedir = adapter_closedir,
    .unlink = adapter_unlink,
    .rename = adapter_rename,
};

int adapter_storage_sd_zephyr_init(adapter_storage_sd_zephyr_t *self,
                                   const char *disk_name,
                                   const char *mount_point)
{
    if (self == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    k_mutex_init(&self->lock);
    atomic_set(&self->in_flight_ops, 0);
    atomic_set(&self->is_mounted, 0);

    const char *d = (disk_name != NULL) ? disk_name : ADAPTER_STORAGE_SD_DEFAULT_DISK_NAME;
    strncpy(self->disk_name, d, sizeof(self->disk_name) - 1);

    const char *m = (mount_point != NULL) ? mount_point : ADAPTER_STORAGE_SD_DEFAULT_MOUNT_POINT;
    strncpy(self->mount_point, m, sizeof(self->mount_point) - 1);

    self->is_initialized = true;
    return 0;
}

i_storage_fs_t adapter_storage_sd_zephyr_bind(adapter_storage_sd_zephyr_t *self)
{
    i_storage_fs_t handle = {
        .api = &g_adapter_storage_sd_api,
        .impl = self,
    };
    return handle;
}

int adapter_storage_sd_zephyr_drain_in_flight(adapter_storage_sd_zephyr_t *self,
                                              k_timeout_t timeout)
{
    if (self == NULL) {
        return -EINVAL;
    }

    int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    while (atomic_get(&self->in_flight_ops) > 0) {
        if (k_uptime_get() >= deadline) {
            LOG_WRN("In-flight operations drain timed out: %ld remaining",
                    (long)atomic_get(&self->in_flight_ops));
            return -ETIMEDOUT;
        }
        k_msleep(5);
    }

    return 0;
}
