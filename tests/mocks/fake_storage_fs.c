/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake filesystem storage port implementation for host & unit testing.
 */

#include "fake_storage_fs.h"
#include <string.h>

static fake_storage_fs_state_t g_fake_fs;

static int find_node_by_path(fake_storage_fs_state_t *s, const char *path)
{
    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_NODES; i++) {
        if (s->nodes[i].exists && strcmp(s->nodes[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_node(fake_storage_fs_state_t *s, const char *path, bool is_dir)
{
    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_NODES; i++) {
        if (!s->nodes[i].exists) {
            memset(&s->nodes[i], 0, sizeof(fake_storage_node_t));
            strncpy(s->nodes[i].path, path, sizeof(s->nodes[i].path) - 1);
            s->nodes[i].exists = true;
            s->nodes[i].is_directory = is_dir;
            s->nodes[i].size = 0;
            return (int)i;
        }
    }
    return -ENOSPC;
}

void fake_storage_fs_reset(void)
{
    memset(&g_fake_fs, 0, sizeof(g_fake_fs));
    g_fake_fs.card_present = true;
    g_fake_fs.is_mounted = false;
    strncpy(g_fake_fs.mount_point, "/SD:", sizeof(g_fake_fs.mount_point) - 1);
}

fake_storage_fs_state_t *fake_storage_fs_get_state(void)
{
    return &g_fake_fs;
}

void fake_storage_fs_set_card_present(bool present)
{
    g_fake_fs.card_present = present;
    if (!present) {
        g_fake_fs.is_mounted = false;
    }
}

void fake_storage_fs_set_error(int err)
{
    g_fake_fs.simulated_error = err;
}

void fake_storage_fs_simulate_card_eject(void)
{
    fake_storage_fs_set_card_present(false);
}

void fake_storage_fs_simulate_card_insert(void)
{
    fake_storage_fs_set_card_present(true);
}

static bool fake_is_card_present(void *impl)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    return s->card_present;
}

static int fake_mount(void *impl, const char *mount_pt)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->mount_calls++;

    if (!s->card_present) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (s->simulated_mount_error != 0) {
        return s->simulated_mount_error;
    }

    if (mount_pt != NULL) {
        strncpy(s->mount_point, mount_pt, sizeof(s->mount_point) - 1);
    }
    s->is_mounted = true;
    return 0;
}

static int fake_unmount(void *impl, const char *mount_pt)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    (void)mount_pt;
    s->unmount_calls++;
    s->is_mounted = false;
    return 0;
}

static int fake_statvfs(void *impl, const char *path, storage_fs_volume_info_t *out_vol)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    (void)path;
    s->statvfs_calls++;

    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }

    out_vol->total_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL; /* 16 GiB */
    out_vol->free_bytes = 15ULL * 1024ULL * 1024ULL * 1024ULL;  /* 15 GiB */
    out_vol->sector_size_bytes = 512;
    out_vol->cluster_size_bytes = 32768;
    strncpy(out_vol->fs_type, "FAT32", sizeof(out_vol->fs_type) - 1);
    strncpy(out_vol->volume_label, "BRIGHTC", sizeof(out_vol->volume_label) - 1);
    return 0;
}

static int fake_open(void *impl, void **out_file_handle, const char *path, uint32_t flags)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->open_calls++;

    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }

    int node_idx = find_node_by_path(s, path);
    if (node_idx >= 0 && s->nodes[node_idx].is_directory) {
        return -EISDIR;
    }

    if (flags & STORAGE_FS_OPEN_CREATE_ALWAYS) {
        if (node_idx >= 0) {
            s->nodes[node_idx].size = 0;
        } else {
            node_idx = alloc_node(s, path, false);
            if (node_idx < 0) {
                return node_idx;
            }
        }
    } else if (flags & STORAGE_FS_OPEN_CREATE_NEW) {
        if (node_idx >= 0) {
            return -EEXIST;
        }
        node_idx = alloc_node(s, path, false);
        if (node_idx < 0) {
            return node_idx;
        }
    } else {
        if (node_idx < 0) {
            return -ENOENT;
        }
    }

    /* Find free file handle */
    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_OPEN_FILES; i++) {
        if (!s->file_handles[i].is_active) {
            s->file_handles[i].node_index = (uint32_t)node_idx;
            s->file_handles[i].cursor = (flags & STORAGE_FS_OPEN_APPEND) ? s->nodes[node_idx].size : 0;
            s->file_handles[i].flags = flags;
            s->file_handles[i].is_active = true;
            *out_file_handle = &s->file_handles[i];
            return 0;
        }
    }

    return -EMFILE;
}

static int fake_close(void *impl, void *file_handle)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    (void)s;
    s->close_calls++;
    if (file_handle == NULL) {
        return -EINVAL;
    }
    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    h->is_active = false;
    return 0;
}

static int fake_read(void *impl, void *file_handle, void *buf, size_t len, size_t *out_read)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->read_calls++;

    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }

    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    if (!h->is_active || h->node_index >= FAKE_STORAGE_FS_MAX_NODES) {
        return -EBADF;
    }

    fake_storage_node_t *node = &s->nodes[h->node_index];
    if (h->cursor >= node->size) {
        if (out_read) {
            *out_read = 0;
        }
        return 0;
    }

    size_t avail = node->size - h->cursor;
    size_t to_read = (len < avail) ? len : avail;
    memcpy(buf, &node->data[h->cursor], to_read);
    h->cursor += to_read;

    if (out_read) {
        *out_read = to_read;
    }
    return 0;
}

static int fake_write(void *impl, void *file_handle, const void *buf, size_t len, size_t *out_written)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->write_calls++;

    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (file_handle == NULL || buf == NULL) {
        return -EINVAL;
    }

    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    if (!h->is_active || h->node_index >= FAKE_STORAGE_FS_MAX_NODES) {
        return -EBADF;
    }

    fake_storage_node_t *node = &s->nodes[h->node_index];
    if (h->cursor + len > FAKE_STORAGE_FS_FILE_MAX_SIZE) {
        return -ENOSPC;
    }

    memcpy(&node->data[h->cursor], buf, len);
    h->cursor += len;
    if (h->cursor > node->size) {
        node->size = h->cursor;
    }

    if (out_written) {
        *out_written = len;
    }
    return 0;
}

static int fake_sync(void *impl, void *file_handle)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    (void)file_handle;
    s->sync_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    return 0;
}

static int fake_seek(void *impl, void *file_handle, int64_t offset, int whence)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->seek_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (file_handle == NULL) {
        return -EINVAL;
    }

    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    if (!h->is_active || h->node_index >= FAKE_STORAGE_FS_MAX_NODES) {
        return -EBADF;
    }

    fake_storage_node_t *node = &s->nodes[h->node_index];
    int64_t new_cursor = 0;

    switch (whence) {
    case STORAGE_FS_SEEK_SET:
        new_cursor = offset;
        break;
    case STORAGE_FS_SEEK_CUR:
        new_cursor = (int64_t)h->cursor + offset;
        break;
    case STORAGE_FS_SEEK_END:
        new_cursor = (int64_t)node->size + offset;
        break;
    default:
        return -EINVAL;
    }

    if (new_cursor < 0 || new_cursor > (int64_t)node->size) {
        return -EINVAL;
    }

    h->cursor = (size_t)new_cursor;
    return 0;
}

static int fake_tell(void *impl, void *file_handle, int64_t *out_offset)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->tell_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (file_handle == NULL || out_offset == NULL) {
        return -EINVAL;
    }

    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    if (!h->is_active) {
        return -EBADF;
    }

    *out_offset = (int64_t)h->cursor;
    return 0;
}

static int fake_truncate(void *impl, void *file_handle, int64_t length)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (file_handle == NULL || length < 0) {
        return -EINVAL;
    }

    fake_storage_file_handle_t *h = (fake_storage_file_handle_t *)file_handle;
    if (!h->is_active || h->node_index >= FAKE_STORAGE_FS_MAX_NODES) {
        return -EBADF;
    }

    fake_storage_node_t *node = &s->nodes[h->node_index];
    if ((size_t)length > FAKE_STORAGE_FS_FILE_MAX_SIZE) {
        return -ENOSPC;
    }

    node->size = (size_t)length;
    if (h->cursor > node->size) {
        h->cursor = node->size;
    }
    return 0;
}

static int fake_mkdir(void *impl, const char *path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->mkdir_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (path == NULL) {
        return -EINVAL;
    }

    int idx = find_node_by_path(s, path);
    if (idx >= 0) {
        return -EEXIST;
    }

    int rc = alloc_node(s, path, true);
    return (rc >= 0) ? 0 : rc;
}

static int fake_stat(void *impl, const char *path, storage_fs_stat_t *out_stat)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->stat_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (path == NULL || out_stat == NULL) {
        return -EINVAL;
    }

    int idx = find_node_by_path(s, path);
    if (idx < 0) {
        return -ENOENT;
    }

    fake_storage_node_t *node = &s->nodes[idx];
    out_stat->size_bytes = node->size;
    out_stat->type = node->is_directory ? STORAGE_FS_ENTRY_DIR : STORAGE_FS_ENTRY_FILE;
    out_stat->modified_timestamp = 1770000000U;

    const char *slash = strrchr(node->path, '/');
    const char *name = (slash != NULL) ? slash + 1 : node->path;
    strncpy(out_stat->name, name, sizeof(out_stat->name) - 1);
    out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    return 0;
}

static int fake_opendir(void *impl, void **out_dir_handle, const char *path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->opendir_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (s->simulated_error != 0) {
        int err = s->simulated_error;
        s->simulated_error = 0;
        return err;
    }
    if (out_dir_handle == NULL || path == NULL) {
        return -EINVAL;
    }

    for (size_t i = 0; i < FAKE_STORAGE_FS_MAX_OPEN_DIRS; i++) {
        if (!s->dir_handles[i].is_active) {
            strncpy(s->dir_handles[i].dir_path, path, sizeof(s->dir_handles[i].dir_path) - 1);
            s->dir_handles[i].current_node_index = 0;
            s->dir_handles[i].is_active = true;
            *out_dir_handle = &s->dir_handles[i];
            return 0;
        }
    }

    return -EMFILE;
}

static int fake_readdir(void *impl, void *dir_handle, storage_fs_dirent_t *out_entry)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->readdir_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (dir_handle == NULL || out_entry == NULL) {
        return -EINVAL;
    }

    fake_storage_dir_handle_t *dh = (fake_storage_dir_handle_t *)dir_handle;
    if (!dh->is_active) {
        return -EBADF;
    }

    size_t prefix_len = strlen(dh->dir_path);
    while (dh->current_node_index < FAKE_STORAGE_FS_MAX_NODES) {
        uint32_t idx = dh->current_node_index++;
        if (!s->nodes[idx].exists) {
            continue;
        }

        /* Check if path starts with directory path */
        if (strncmp(s->nodes[idx].path, dh->dir_path, prefix_len) == 0) {
            const char *sub = s->nodes[idx].path + prefix_len;
            if (*sub == '/') {
                sub++;
            }
            if (*sub == '\0') {
                continue; /* Matches directory itself */
            }
            /* Do not recurse into subdirectories */
            if (strchr(sub, '/') != NULL) {
                continue;
            }

            out_entry->size_bytes = s->nodes[idx].size;
            out_entry->type = s->nodes[idx].is_directory ? STORAGE_FS_ENTRY_DIR : STORAGE_FS_ENTRY_FILE;
            strncpy(out_entry->name, sub, sizeof(out_entry->name) - 1);
            out_entry->name[sizeof(out_entry->name) - 1] = '\0';
            return 0;
        }
    }

    out_entry->name[0] = '\0';
    return 0;
}

static int fake_closedir(void *impl, void *dir_handle)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    (void)s;
    s->closedir_calls++;
    if (dir_handle == NULL) {
        return -EINVAL;
    }
    fake_storage_dir_handle_t *dh = (fake_storage_dir_handle_t *)dir_handle;
    dh->is_active = false;
    return 0;
}

static int fake_unlink(void *impl, const char *path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->unlink_calls++;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (path == NULL) {
        return -EINVAL;
    }

    int idx = find_node_by_path(s, path);
    if (idx < 0) {
        return -ENOENT;
    }

    s->nodes[idx].exists = false;
    return 0;
}

static int fake_rename(void *impl, const char *old_path, const char *new_path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    if (!s->is_mounted) {
        return -ENODEV;
    }
    if (old_path == NULL || new_path == NULL) {
        return -EINVAL;
    }

    int idx = find_node_by_path(s, old_path);
    if (idx < 0) {
        return -ENOENT;
    }

    strncpy(s->nodes[idx].path, new_path, sizeof(s->nodes[idx].path) - 1);
    return 0;
}

static const i_storage_fs_api_t g_fake_fs_api = {
    .is_card_present = fake_is_card_present,
    .mount = fake_mount,
    .unmount = fake_unmount,
    .statvfs = fake_statvfs,
    .open = fake_open,
    .close = fake_close,
    .read = fake_read,
    .write = fake_write,
    .sync = fake_sync,
    .seek = fake_seek,
    .tell = fake_tell,
    .truncate = fake_truncate,
    .mkdir = fake_mkdir,
    .stat = fake_stat,
    .opendir = fake_opendir,
    .readdir = fake_readdir,
    .closedir = fake_closedir,
    .unlink = fake_unlink,
    .rename = fake_rename,
};

i_storage_fs_t fake_storage_fs_get_port(void)
{
    i_storage_fs_t port = {
        .api = &g_fake_fs_api,
        .impl = &g_fake_fs,
    };
    return port;
}

i_storage_fs_t fake_storage_fs_get_handle(void)
{
    return fake_storage_fs_get_port();
}
