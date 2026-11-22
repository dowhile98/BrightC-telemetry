/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file datalogger_streamer.c
 * @brief Chunked HTTP/REST Streaming Service Implementation.
 */

#include "application/services/datalogger_streamer.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(datalogger_streamer, CONFIG_LOG_DEFAULT_LEVEL);

int datalogger_streamer_init(datalogger_streamer_t *self,
                             const i_storage_fs_t *fs,
                             ao_datalogger_t *datalogger_ao,
                             const char *mount_point)
{
    if (self == NULL || fs == NULL || mount_point == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->fs = *fs;
    self->datalogger_ao = datalogger_ao;
    strncpy(self->mount_point, mount_point, sizeof(self->mount_point) - 1U);
    self->mount_point[sizeof(self->mount_point) - 1U] = '\0';
    self->is_initialized = true;

    return 0;
}

bool datalogger_streamer_is_valid_date(const char *date_str)
{
    if (date_str == NULL || strlen(date_str) != 10U) {
        return false;
    }

    /* Reject traversal tokens */
    if (strstr(date_str, "..") != NULL || strchr(date_str, '/') != NULL || strchr(date_str, '\\') != NULL) {
        return false;
    }

    /* Verify YYYY-MM-DD pattern */
    for (size_t i = 0; i < 10U; i++) {
        if (i == 4U || i == 7U) {
            if (date_str[i] != '-') {
                return false;
            }
        } else {
            if (!isdigit((unsigned char)date_str[i])) {
                return false;
            }
        }
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(date_str, "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }

    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    return true;
}

int datalogger_streamer_stream_file(datalogger_streamer_t *self,
                                    log_stream_t stream_type,
                                    const char *date_str,
                                    datalogger_stream_sink_cb_t sink_cb,
                                    void *user_data,
                                    size_t *bytes_sent)
{
    if (self == NULL || !self->is_initialized || sink_cb == NULL) {
        return -EINVAL;
    }

    if (!datalogger_streamer_is_valid_date(date_str)) {
        LOG_WRN("Invalid date query: %s", date_str != NULL ? date_str : "NULL");
        return -EINVAL;
    }

    /* If querying the current calendar date, flush pending active stream buffers first */
    if (self->datalogger_ao != NULL && strcmp(self->datalogger_ao->current_date, date_str) == 0) {
        (void)ao_datalogger_flush(self->datalogger_ao);
        k_sleep(K_MSEC(20));
    }

    int year = 0;
    int month = 0;
    int day = 0;
    (void)sscanf(date_str, "%d-%d-%d", &year, &month, &day);

    char path[FS_MAX_PATH_LEN];
    storage_fs_stat_t st;

    switch (stream_type) {
    case LOG_STREAM_TEMPERATURE:
        snprintf(path, sizeof(path), "%s/history/temperature/%04d/%02d/%s_temperature.csv",
                 self->mount_point, year, month, date_str);
        break;

    case LOG_STREAM_ENERGY:
        snprintf(path, sizeof(path), "%s/history/energy/%04d/%02d/%s_energy.csv",
                 self->mount_point, year, month, date_str);
        break;

    case LOG_STREAM_THERMAL:
        snprintf(path, sizeof(path), "%s/history/thermal/%04d/%02d/%s_thermal.csv",
                 self->mount_point, year, month, date_str);
        if (i_storage_fs_stat(&self->fs, path, &st) != 0) {
            snprintf(path, sizeof(path), "%s/history/%04d/%02d/%s_telemetry.csv",
                     self->mount_point, year, month, date_str);
        }
        break;

    case LOG_STREAM_ALARMS:
        snprintf(path, sizeof(path), "%s/logs/alarms/%04d/%02d/%s_alarms.csv",
                 self->mount_point, year, month, date_str);
        if (i_storage_fs_stat(&self->fs, path, &st) != 0) {
            snprintf(path, sizeof(path), "%s/logs/%04d/%02d/%s_alarms.log",
                     self->mount_point, year, month, date_str);
        }
        break;

    case LOG_STREAM_SYSTEM:
        snprintf(path, sizeof(path), "%s/logs/system/%04d/%02d/%s_system.log",
                 self->mount_point, year, month, date_str);
        if (i_storage_fs_stat(&self->fs, path, &st) != 0) {
            snprintf(path, sizeof(path), "%s/logs/%04d/%02d/%s_system.log",
                     self->mount_point, year, month, date_str);
        }
        break;

    default:
        snprintf(path, sizeof(path), "%s/history/%04d/%02d/%s_telemetry.csv",
                 self->mount_point, year, month, date_str);
        break;
    }

    void *file_handle = NULL;
    int rc = i_storage_fs_open(&self->fs, &file_handle, path, STORAGE_FS_OPEN_READ);
    if (rc != 0) {
        LOG_INF("Stream file not found: %s (rc=%d)", path, rc);
        return -ENOENT;
    }

    uint8_t chunk[DATALOGGER_STREAMER_CHUNK_SIZE];
    size_t total_sent = 0U;
    int stream_rc = 0;

    while (true) {
        size_t read_bytes = 0U;
        rc = i_storage_fs_read(&self->fs, file_handle, chunk, sizeof(chunk), &read_bytes);
        if (rc != 0) {
            LOG_ERR("Read error on stream %s: %d", path, rc);
            stream_rc = -EIO;
            break;
        }

        if (read_bytes == 0U) {
            /* Clean EOF */
            break;
        }

        int sink_rc = sink_cb(chunk, read_bytes, user_data);
        if (sink_rc != 0) {
            LOG_WRN("Sink callback aborted stream at %zu bytes", total_sent);
            stream_rc = sink_rc;
            break;
        }

        total_sent += read_bytes;
    }

    (void)i_storage_fs_close(&self->fs, file_handle);

    if (bytes_sent != NULL) {
        *bytes_sent = total_sent;
    }

    return stream_rc;
}
