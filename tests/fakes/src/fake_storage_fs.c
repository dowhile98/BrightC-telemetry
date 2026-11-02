/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake filesystem storage port implementation for host unit testing.
 */

#include "fake_storage_fs.h"
#include <string.h>

static fake_storage_fs_state_t g_fake_fs;

void fake_storage_fs_reset(void)
{
    memset(&g_fake_fs, 0, sizeof(g_fake_fs));
    g_fake_fs.card_present = true;
    g_fake_fs.mounted = true;
}

fake_storage_fs_state_t *fake_storage_fs_get_state(void)
{
    return &g_fake_fs;
}

void fake_storage_fs_simulate_card_eject(void)
{
    g_fake_fs.card_present = false;
    g_fake_fs.mounted = false;
}

void fake_storage_fs_simulate_card_insert(void)
{
    g_fake_fs.card_present = true;
}

static int fake_mount(void *impl)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->mount_calls++;
    if (!s->card_present) {
        return -ENODEV;
    }
    if (s->simulated_mount_error != 0) {
        return s->simulated_mount_error;
    }
    s->mounted = true;
    return 0;
}

static int fake_unmount(void *impl)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->unmount_calls++;
    s->mounted = false;
    return 0;
}

static bool fake_is_mounted(void *impl)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    return s->mounted;
}

static bool fake_is_card_present(void *impl)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    return s->card_present;
}

static int fake_get_volume_info(void *impl, fs_volume_info_t *out_info)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    if (!s->mounted) {
        return -ENODEV;
    }
    out_info->total_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL; /* 16 GB */
    out_info->free_bytes = 15ULL * 1024ULL * 1024ULL * 1024ULL;
    out_info->sector_size_bytes = 512;
    out_info->cluster_size_bytes = 32768;
    strncpy(out_info->fs_type, "FAT32", sizeof(out_info->fs_type) - 1);
    strncpy(out_info->volume_label, "BRIGHTC", sizeof(out_info->volume_label) - 1);
    return 0;
}

static int find_node_by_name(fake_storage_fs_state_t *s, const char *path)
{
    for (size_t i = 0; i < FAKE_FS_MAX_FILES; i++) {
        if (s->nodes[i].exists && strcmp(s->nodes[i].name, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int fake_file_open(void *impl, fs_file_t *out_file, const char *path, uint32_t flags)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->open_calls++;

    if (!s->mounted) {
        return -ENODEV;
    }
    if (s->simulated_io_error != 0) {
        return s->simulated_io_error;
    }

    int node_idx = find_node_by_name(s, path);
    if (node_idx < 0) {
        if (!(flags & (FS_OPEN_CREATE_ALWAYS | FS_OPEN_CREATE_NEW))) {
            return -ENOENT;
        }
        /* Allocate node */
        for (size_t i = 0; i < FAKE_FS_MAX_FILES; i++) {
            if (!s->nodes[i].exists) {
                node_idx = (int)i;
                s->nodes[i].exists = true;
                s->nodes[i].size = 0;
                s->nodes[i].is_directory = false;
                strncpy(s->nodes[i].name, path, sizeof(s->nodes[i].name) - 1);
                break;
            }
        }
        if (node_idx < 0) {
            return -ENOSPC;
        }
    } else {
        if (flags & FS_OPEN_CREATE_ALWAYS) {
            s->nodes[node_idx].size = 0;
        }
    }

    /* Allocate open handle */
    for (size_t h = 0; h < FAKE_FS_MAX_OPEN_HANDLES; h++) {
        if (!s->handles[h].is_active) {
            s->handles[h].is_active = true;
            s->handles[h].node_index = (uint32_t)node_idx;
            s->handles[h].flags = flags;
            s->handles[h].cursor = (flags & FS_OPEN_APPEND) ? s->nodes[node_idx].size : 0;
            *out_file = (fs_file_t)&s->handles[h];
            return 0;
        }
    }

    return -EMFILE;
}

static int fake_file_close(void *impl, fs_file_t file)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->close_calls++;
    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }
    h->is_active = false;
    return 0;
}

static int fake_file_read(void *impl, fs_file_t file, void *buf, size_t count, size_t *out_bytes_read)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->read_calls++;

    if (!s->mounted) {
        return -ENODEV;
    }
    if (s->simulated_io_error != 0) {
        return s->simulated_io_error;
    }

    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }

    fake_fs_node_t *node = &s->nodes[h->node_index];
    if (h->cursor >= node->size) {
        if (out_bytes_read != NULL) {
            *out_bytes_read = 0;
        }
        return 0; /* EOF */
    }

    size_t available = node->size - h->cursor;
    size_t to_read = (count < available) ? count : available;
    memcpy(buf, &node->data[h->cursor], to_read);
    h->cursor += to_read;

    if (out_bytes_read != NULL) {
        *out_bytes_read = to_read;
    }
    return 0;
}

static int fake_file_write(void *impl, fs_file_t file, const void *buf, size_t count, size_t *out_bytes_written)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->write_calls++;

    if (!s->mounted) {
        return -ENODEV;
    }
    if (s->simulated_disk_full) {
        return -ENOSPC;
    }
    if (s->simulated_io_error != 0) {
        return s->simulated_io_error;
    }

    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }

    fake_fs_node_t *node = &s->nodes[h->node_index];
    if (h->cursor + count > FAKE_FS_FILE_MAX_SIZE) {
        return -ENOSPC;
    }

    memcpy(&node->data[h->cursor], buf, count);
    h->cursor += count;
    if (h->cursor > node->size) {
        node->size = h->cursor;
    }

    if (out_bytes_written != NULL) {
        *out_bytes_written = count;
    }
    return 0;
}

static int fake_file_seek(void *impl, fs_file_t file, int64_t offset, fs_seek_origin_t origin)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }
    fake_fs_node_t *node = &s->nodes[h->node_index];

    int64_t new_cursor = 0;
    switch (origin) {
    case FS_SEEK_SET:
        new_cursor = offset;
        break;
    case FS_SEEK_CUR:
        new_cursor = (int64_t)h->cursor + offset;
        break;
    case FS_SEEK_END:
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

static int fake_file_tell(void *impl, fs_file_t file, int64_t *out_offset)
{
    (void)impl;
    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }
    *out_offset = (int64_t)h->cursor;
    return 0;
}

static int fake_file_sync(void *impl, fs_file_t file)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    s->sync_calls++;
    (void)file;
    return 0;
}

static int fake_file_truncate(void *impl, fs_file_t file, int64_t length)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    fake_fs_open_file_t *h = (fake_fs_open_file_t *)file;
    if (h == NULL || !h->is_active) {
        return -EBADF;
    }
    fake_fs_node_t *node = &s->nodes[h->node_index];
    if (length < 0 || (size_t)length > FAKE_FS_FILE_MAX_SIZE) {
        return -EINVAL;
    }
    node->size = (size_t)length;
    if (h->cursor > node->size) {
        h->cursor = node->size;
    }
    return 0;
}

static int fake_dir_open(void *impl, fs_dir_t *out_dir, const char *path)
{
    (void)impl;
    (void)out_dir;
    (void)path;
    return 0;
}

static int fake_dir_read(void *impl, fs_dir_t dir, fs_dirent_t *out_entry)
{
    (void)impl;
    (void)dir;
    (void)out_entry;
    return 0;
}

static int fake_dir_close(void *impl, fs_dir_t dir)
{
    (void)impl;
    (void)dir;
    return 0;
}

static int fake_mkdir(void *impl, const char *path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    if (find_node_by_name(s, path) >= 0) {
        return -EEXIST;
    }
    for (size_t i = 0; i < FAKE_FS_MAX_FILES; i++) {
        if (!s->nodes[i].exists) {
            s->nodes[i].exists = true;
            s->nodes[i].is_directory = true;
            strncpy(s->nodes[i].name, path, sizeof(s->nodes[i].name) - 1);
            return 0;
        }
    }
    return -ENOSPC;
}

static int fake_unlink(void *impl, const char *path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    int idx = find_node_by_name(s, path);
    if (idx < 0) {
        return -ENOENT;
    }
    s->nodes[idx].exists = false;
    return 0;
}

static int fake_rename(void *impl, const char *old_path, const char *new_path)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    int idx = find_node_by_name(s, old_path);
    if (idx < 0) {
        return -ENOENT;
    }
    strncpy(s->nodes[idx].name, new_path, sizeof(s->nodes[idx].name) - 1);
    return 0;
}

static int fake_stat(void *impl, const char *path, fs_stat_t *out_stat)
{
    fake_storage_fs_state_t *s = (fake_storage_fs_state_t *)impl;
    int idx = find_node_by_name(s, path);
    if (idx < 0) {
        return -ENOENT;
    }
    out_stat->size_bytes = s->nodes[idx].size;
    out_stat->type = s->nodes[idx].is_directory ? FS_ENTRY_TYPE_DIR : FS_ENTRY_TYPE_FILE;
    out_stat->modified_timestamp = 0;
    strncpy(out_stat->name, path, sizeof(out_stat->name) - 1);
    return 0;
}

static const i_storage_fs_api_t g_fake_fs_api = {
    .mount = fake_mount,
    .unmount = fake_unmount,
    .is_mounted = fake_is_mounted,
    .is_card_present = fake_is_card_present,
    .get_volume_info = fake_get_volume_info,
    .file_open = fake_file_open,
    .file_close = fake_file_close,
    .file_read = fake_file_read,
    .file_write = fake_file_write,
    .file_seek = fake_file_seek,
    .file_tell = fake_file_tell,
    .file_sync = fake_file_sync,
    .file_truncate = fake_file_truncate,
    .dir_open = fake_dir_open,
    .dir_read = fake_dir_read,
    .dir_close = fake_dir_close,
    .mkdir = fake_mkdir,
    .unlink = fake_unlink,
    .rename = fake_rename,
    .stat = fake_stat,
};

i_storage_fs_t fake_storage_fs_get_handle(void)
{
    i_storage_fs_t handle = {
        .api = &g_fake_fs_api,
        .impl = &g_fake_fs,
    };
    return handle;
}
