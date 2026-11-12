/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for in-memory Fake Storage FS port.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "fake_storage_fs.h"

static void *fake_storage_fs_setup(void)
{
    fake_storage_fs_reset();
    return NULL;
}

static void fake_storage_fs_before(void *fixture)
{
    (void)fixture;
    fake_storage_fs_reset();
}

ZTEST_SUITE(fake_storage_fs_suite, NULL, fake_storage_fs_setup, fake_storage_fs_before, NULL, NULL);

ZTEST(fake_storage_fs_suite, test_fake_fs_binding_and_presence)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    zassert_true(i_storage_fs_is_bound(&fs), "Port must be validly bound");

    zassert_true(i_storage_fs_is_card_present(&fs), "Card must be initially present");

    fake_storage_fs_set_card_present(false);
    zassert_false(i_storage_fs_is_card_present(&fs), "Card must be absent");

    /* Attempt to mount without card */
    int rc = i_storage_fs_mount(&fs, "/SD:");
    zassert_equal(rc, -ENODEV, "Mounting without card must return -ENODEV");

    fake_storage_fs_set_card_present(true);
    zassert_true(i_storage_fs_is_card_present(&fs), "Card must be present after insert");
}

ZTEST(fake_storage_fs_suite, test_fake_fs_mount_unmount)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    storage_fs_volume_info_t vol;

    int rc = i_storage_fs_mount(&fs, "/SD:");
    zassert_equal(rc, 0, "Mount must succeed: %d", rc);

    rc = i_storage_fs_statvfs(&fs, "/SD:", &vol);
    zassert_equal(rc, 0, "statvfs must succeed: %d", rc);
    zassert_true(vol.total_bytes > 0, "Total bytes must be > 0");
    zassert_true(vol.free_bytes > 0, "Free bytes must be > 0");
    zassert_str_equal(vol.fs_type, "FAT32", "FS type should be FAT32");

    rc = i_storage_fs_unmount(&fs, "/SD:");
    zassert_equal(rc, 0, "Unmount must succeed: %d", rc);

    /* statvfs after unmount should fail */
    rc = i_storage_fs_statvfs(&fs, "/SD:", &vol);
    zassert_equal(rc, -ENODEV, "statvfs after unmount must return -ENODEV");
}

ZTEST(fake_storage_fs_suite, test_fake_fs_file_io)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    zassert_equal(i_storage_fs_mount(&fs, "/SD:"), 0, "Mount failed");

    void *file = NULL;
    int rc = i_storage_fs_open(&fs, &file, "/SD:/test.txt",
                               STORAGE_FS_OPEN_CREATE_ALWAYS | STORAGE_FS_OPEN_WRITE);
    zassert_equal(rc, 0, "Open file failed: %d", rc);
    zassert_not_null(file, "File handle must not be NULL");

    const char write_data[] = "BrightC MicroSD Test Payload 123456789";
    size_t written = 0;
    rc = i_storage_fs_write(&fs, file, write_data, strlen(write_data), &written);
    zassert_equal(rc, 0, "Write failed: %d", rc);
    zassert_equal(written, strlen(write_data), "Bytes written mismatch");

    rc = i_storage_fs_sync(&fs, file);
    zassert_equal(rc, 0, "Sync failed: %d", rc);

    int64_t offset = 0;
    rc = i_storage_fs_tell(&fs, file, &offset);
    zassert_equal(rc, 0, "Tell failed: %d", rc);
    zassert_equal(offset, (int64_t)strlen(write_data), "Offset mismatch");

    rc = i_storage_fs_close(&fs, file);
    zassert_equal(rc, 0, "Close failed: %d", rc);

    /* Re-open for reading */
    file = NULL;
    rc = i_storage_fs_open(&fs, &file, "/SD:/test.txt", STORAGE_FS_OPEN_READ);
    zassert_equal(rc, 0, "Open for read failed: %d", rc);

    /* Seek to offset 8 ("MicroSD...") */
    rc = i_storage_fs_seek(&fs, file, 8, STORAGE_FS_SEEK_SET);
    zassert_equal(rc, 0, "Seek failed: %d", rc);

    char read_buf[64] = {0};
    size_t read_bytes = 0;
    rc = i_storage_fs_read(&fs, file, read_buf, sizeof(read_buf) - 1, &read_bytes);
    zassert_equal(rc, 0, "Read failed: %d", rc);
    zassert_str_equal(read_buf, write_data + 8, "Read data mismatch");

    /* Truncate test */
    rc = i_storage_fs_truncate(&fs, file, 15);
    zassert_equal(rc, 0, "Truncate failed: %d", rc);

    rc = i_storage_fs_close(&fs, file);
    zassert_equal(rc, 0, "Close after truncate failed: %d", rc);
}

ZTEST(fake_storage_fs_suite, test_fake_fs_dir_and_stat)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    zassert_equal(i_storage_fs_mount(&fs, "/SD:"), 0, "Mount failed");

    int rc = i_storage_fs_mkdir(&fs, "/SD:/history");
    zassert_equal(rc, 0, "mkdir failed: %d", rc);

    storage_fs_stat_t st;
    rc = i_storage_fs_stat(&fs, "/SD:/history", &st);
    zassert_equal(rc, 0, "stat on directory failed: %d", rc);
    zassert_equal(st.type, STORAGE_FS_ENTRY_DIR, "Should be directory");

    /* Create file inside /SD:/history */
    void *file = NULL;
    rc = i_storage_fs_open(&fs, &file, "/SD:/history/telemetry.csv",
                           STORAGE_FS_OPEN_CREATE_ALWAYS | STORAGE_FS_OPEN_WRITE);
    zassert_equal(rc, 0, "Open file in dir failed: %d", rc);
    i_storage_fs_close(&fs, file);

    rc = i_storage_fs_stat(&fs, "/SD:/history/telemetry.csv", &st);
    zassert_equal(rc, 0, "stat on file failed: %d", rc);
    zassert_equal(st.type, STORAGE_FS_ENTRY_FILE, "Should be regular file");

    /* Open and read directory */
    void *dir = NULL;
    rc = i_storage_fs_opendir(&fs, &dir, "/SD:/history");
    zassert_equal(rc, 0, "opendir failed: %d", rc);

    storage_fs_dirent_t dent;
    rc = i_storage_fs_readdir(&fs, dir, &dent);
    zassert_equal(rc, 0, "readdir failed: %d", rc);
    zassert_str_equal(dent.name, "telemetry.csv", "Directory entry mismatch");

    rc = i_storage_fs_closedir(&fs, dir);
    zassert_equal(rc, 0, "closedir failed: %d", rc);

    /* Unlink file */
    rc = i_storage_fs_unlink(&fs, "/SD:/history/telemetry.csv");
    zassert_equal(rc, 0, "unlink failed: %d", rc);

    rc = i_storage_fs_stat(&fs, "/SD:/history/telemetry.csv", &st);
    zassert_equal(rc, -ENOENT, "stat on deleted file must return -ENOENT");
}

ZTEST(fake_storage_fs_suite, test_fake_fs_error_injection)
{
    i_storage_fs_t fs = fake_storage_fs_get_port();
    fake_storage_fs_set_error(-EIO);

    int rc = i_storage_fs_mount(&fs, "/SD:");
    zassert_equal(rc, -EIO, "Injected error must be returned: %d", rc);
}
