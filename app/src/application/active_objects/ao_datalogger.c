/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_datalogger.c
 * @brief Unified Historical Telemetry & Event-Driven Datalogger Active Object Implementation.
 */

#include "application/active_objects/ao_datalogger.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "domain/time/rtc_time_calc.h"

LOG_MODULE_REGISTER(ao_datalogger, CONFIG_LOG_DEFAULT_LEVEL);

#define CSV_ALARMS_HEADER \
    "# schema_version=1;source=BrightC_V3;medium=SDMMC\n" \
    "Timestamp_ISO,Uptime_ms,Seq_ID,Alarm_ID,Subsystem,Transition,Threshold,Measured,Severity,Tag\n"

#define CSV_BOOT_HEADER \
    "# schema_version=1;source=BrightC_V3;type=boot_staging\n" \
    "Uptime_ms,Seq_ID,Stream,Event_ID,Message\n"

static const char *severity_to_str(alarm_severity_t sev)
{
    switch (sev) {
    case ALARM_SEVERITY_INFO:     return "INF";
    case ALARM_SEVERITY_WARNING:  return "WRN";
    case ALARM_SEVERITY_CRITICAL: return "CRT";
    case ALARM_SEVERITY_FATAL:    return "FTL";
    default:                      return "UNK";
    }
}

static const char *transition_to_str(alarm_transition_type_t t)
{
    switch (t) {
    case ALARM_EVENT_ACTIVATED:   return "TRIP";
    case ALARM_EVENT_CLEARED:     return "CLEAR";
    case ALARM_EVENT_ACKNOWLEDGED:return "ACK";
    case ALARM_EVENT_LATCHED:     return "LATCH";
    default:                      return "UNKNOWN";
    }
}

static const char *subsystem_to_str(alarm_source_category_t cat)
{
    switch (cat) {
    case ALARM_SOURCE_DIGITAL_INPUT: return "DIGITAL_IN";
    case ALARM_SOURCE_SENSOR_ANALOG: return "SENSORS";
    case ALARM_SOURCE_ELECTRICAL:    return "POWER";
    case ALARM_SOURCE_HVAC_CONTROL:  return "HVAC";
    case ALARM_SOURCE_SYSTEM_INFRA:  return "SYSTEM";
    default:                         return "CUSTOM";
    }
}

static void get_iso_timestamp(char *buf, size_t len, int64_t epoch_s, uint32_t ms)
{
    if (epoch_s <= 0) {
        snprintf(buf, len, "UNSYNCHRONIZED");
        return;
    }

    rtc_date_time_t dt;
    if (rtc_calc_epoch_to_datetime(epoch_s, (uint16_t)(ms % 1000U), &dt) == 0) {
        (void)rtc_calc_format_iso8601(&dt, buf, len);
    } else {
        snprintf(buf, len, "UNSYNCHRONIZED");
    }
}

static void ensure_directory_path(ao_datalogger_t *self, const char *dir_path)
{
    storage_fs_stat_t st;
    if (i_storage_fs_stat(&self->fs, dir_path, &st) != 0) {
        (void)i_storage_fs_mkdir(&self->fs, dir_path);
    }
}

static void flush_stream_buffer(ao_datalogger_t *self,
                                void **file_handle,
                                const char *file_path,
                                const char *header_csv,
                                uint8_t *buf,
                                size_t *buf_len)
{
    if (*buf_len == 0U || !self->is_sd_mounted) {
        return;
    }

    if (*file_handle == NULL) {
        uint32_t flags = STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_APPEND;
        storage_fs_stat_t st;
        bool exists = (i_storage_fs_stat(&self->fs, file_path, &st) == 0);

        if (!exists) {
            flags |= STORAGE_FS_OPEN_CREATE_ALWAYS;
        }

        int rc = i_storage_fs_open(&self->fs, file_handle, file_path, flags);
        if (rc != 0) {
            LOG_ERR("Failed to open %s: %d", file_path, rc);
            return;
        }

        if (!exists && header_csv != NULL) {
            size_t written = 0;
            (void)i_storage_fs_write(&self->fs, *file_handle, header_csv, strlen(header_csv), &written);
        }
    }

    size_t written = 0;
    int rc = i_storage_fs_write(&self->fs, *file_handle, buf, *buf_len, &written);
    if (rc == 0) {
        self->stats.sectors_written += (uint32_t)((*buf_len + AO_DATALOGGER_SECTOR_SIZE - 1U) / AO_DATALOGGER_SECTOR_SIZE);
        *buf_len = 0U;
    } else {
        LOG_ERR("Write error on %s: %d", file_path, rc);
    }
}

static void close_stream_file(ao_datalogger_t *self, void **file_handle)
{
    if (*file_handle != NULL) {
        (void)i_storage_fs_sync(&self->fs, *file_handle);
        (void)i_storage_fs_close(&self->fs, *file_handle);
        *file_handle = NULL;
    }
}

static void flush_all_streams(ao_datalogger_t *self)
{
    char path_buf[FS_MAX_PATH_LEN];

    /* 1. Alarms Stream */
    if (self->alarms_buf_len > 0U && self->is_sd_mounted) {
        snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_alarms.log",
                 self->mount_point, self->current_date, self->current_date);
        flush_stream_buffer(self, &self->alarms_file, path_buf, CSV_ALARMS_HEADER,
                            self->alarms_buf, &self->alarms_buf_len);
    }

    /* 2. System Stream */
    if (self->system_buf_len > 0U && self->is_sd_mounted) {
        snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_system.log",
                 self->mount_point, self->current_date, self->current_date);
        flush_stream_buffer(self, &self->system_file, path_buf, NULL,
                            self->system_buf, &self->system_buf_len);
    }

    /* 3. Boot Staging Stream */
    if (self->boot_buf_len > 0U && self->is_sd_mounted) {
        snprintf(path_buf, sizeof(path_buf), "%s/logs/boot/boot_%u_staging.csv",
                 self->mount_point, (unsigned int)self->boot_count);
        flush_stream_buffer(self, &self->boot_file, path_buf, CSV_BOOT_HEADER,
                            self->boot_buf, &self->boot_buf_len);
    }

    if (self->alarms_file != NULL) {
        (void)i_storage_fs_sync(&self->fs, self->alarms_file);
    }
    if (self->system_file != NULL) {
        (void)i_storage_fs_sync(&self->fs, self->system_file);
    }
    if (self->boot_file != NULL) {
        (void)i_storage_fs_sync(&self->fs, self->boot_file);
    }

    self->last_flush_uptime_ms = k_uptime_get();
    self->stats.flushes_performed++;
}

static void append_to_stream(ao_datalogger_t *self,
                             uint8_t *buf,
                             size_t *buf_len,
                             const char *line,
                             size_t line_len,
                             void **file_handle,
                             const char *file_path,
                             const char *header_csv)
{
    if (!self->is_sd_mounted) {
        *buf_len = 0U;
        return;
    }

    if (line_len >= AO_DATALOGGER_SECTOR_SIZE) {
        /* Flush pending */
        flush_stream_buffer(self, file_handle, file_path, header_csv, buf, buf_len);
        /* Write oversized line directly */
        if (*file_handle != NULL) {
            size_t written = 0;
            (void)i_storage_fs_write(&self->fs, *file_handle, line, line_len, &written);
        }
        return;
    }

    if (*buf_len + line_len > AO_DATALOGGER_SECTOR_SIZE) {
        flush_stream_buffer(self, file_handle, file_path, header_csv, buf, buf_len);
        /* If flush failed to drain buffer, reset buffer to prevent memory overflow */
        if (*buf_len + line_len > AO_DATALOGGER_SECTOR_SIZE) {
            *buf_len = 0U;
            self->stats.events_dropped++;
        }
    }

    memcpy(buf + *buf_len, line, line_len);
    *buf_len += line_len;
}

static void reconcile_bkpsram(ao_datalogger_t *self)
{
    if (!i_bkpsram_is_bound(&self->bkp) || !self->is_sd_mounted) {
        return;
    }

    uint32_t cursor = 0;
    alarm_nv_record_t rec;
    char line_buf[256];
    char time_str[32];
    char path_buf[FS_MAX_PATH_LEN];

    snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_alarms.log",
             self->mount_point, self->current_date, self->current_date);

    while (i_bkpsram_get_next_uncommitted(&self->bkp, cursor, &rec, &cursor) == 0) {
        get_iso_timestamp(time_str, sizeof(time_str), (int64_t)rec.epoch_s, (uint32_t)(rec.uptime_ms % 1000ULL));

        int len = snprintf(line_buf, sizeof(line_buf),
                           "%s,%llu,%u,%u,%s,%s,%.2f,%.2f,%s,%s # [RECOVERED_FROM_BKPSRAM]\n",
                           time_str,
                           (unsigned long long)rec.uptime_ms,
                           (unsigned int)rec.seq_id,
                           (unsigned int)rec.alarm_id,
                           subsystem_to_str((alarm_source_category_t)rec.source_subsystem),
                           transition_to_str((alarm_transition_type_t)rec.transition_type),
                           (double)rec.threshold_value,
                           (double)rec.measured_value,
                           severity_to_str((alarm_severity_t)rec.severity),
                           rec.tag);

        if (len > 0) {
            append_to_stream(self, self->alarms_buf, &self->alarms_buf_len,
                             line_buf, (size_t)len, &self->alarms_file, path_buf, CSV_ALARMS_HEADER);
            (void)i_bkpsram_mark_committed(&self->bkp, rec.seq_id);
            self->stats.bkpsram_recovered++;
        }
    }
}

static void check_storage_low_watermark(ao_datalogger_t *self)
{
    if (!self->is_sd_mounted) {
        return;
    }

    storage_fs_volume_info_t vol;
    int rc = i_storage_fs_statvfs(&self->fs, self->mount_point, &vol);
    if (rc != 0) {
        return;
    }

    uint32_t free_mb = (uint32_t)(vol.free_bytes / (1024ULL * 1024ULL));
    if (free_mb > AO_DATALOGGER_LOW_WATERMARK_MB) {
        return;
    }

    LOG_WRN("Storage low-watermark reached (%u MB <= %u MB). Initiating prune.",
            (unsigned int)free_mb, (unsigned int)AO_DATALOGGER_LOW_WATERMARK_MB);

    char logs_dir[FS_MAX_PATH_LEN];
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", self->mount_point);

    void *dir_handle = NULL;
    if (i_storage_fs_opendir(&self->fs, &dir_handle, logs_dir) != 0) {
        return;
    }

    storage_fs_dirent_t entry;
    char oldest_dir[FS_MAX_FILENAME_LEN] = {0};

    while (i_storage_fs_readdir(&self->fs, dir_handle, &entry) == 0 && entry.name[0] != '\0') {
        if (entry.type == STORAGE_FS_ENTRY_DIR && strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0 &&
            strcmp(entry.name, "boot") != 0 && strcmp(entry.name, self->current_date) != 0) {
            if (oldest_dir[0] == '\0' || strcmp(entry.name, oldest_dir) < 0) {
                strncpy(oldest_dir, entry.name, sizeof(oldest_dir) - 1U);
            }
        }
    }
    (void)i_storage_fs_closedir(&self->fs, dir_handle);

    if (oldest_dir[0] != '\0') {
        char target_dir[FS_MAX_PATH_LEN];
        snprintf(target_dir, sizeof(target_dir), "%s/logs/%s", self->mount_point, oldest_dir);

        /* Delete files inside target directory first (FAT requirement) */
        if (i_storage_fs_opendir(&self->fs, &dir_handle, target_dir) == 0) {
            while (i_storage_fs_readdir(&self->fs, dir_handle, &entry) == 0 && entry.name[0] != '\0') {
                if (entry.type == STORAGE_FS_ENTRY_FILE) {
                    char file_to_del[FS_MAX_PATH_LEN];
                    size_t tlen = strlen(target_dir);
                    size_t nlen = strlen(entry.name);
                    if (tlen + 1U + nlen < sizeof(file_to_del)) {
                        memcpy(file_to_del, target_dir, tlen);
                        file_to_del[tlen] = '/';
                        memcpy(file_to_del + tlen + 1U, entry.name, nlen + 1U);
                        (void)i_storage_fs_unlink(&self->fs, file_to_del);
                    }
                }
            }
            (void)i_storage_fs_closedir(&self->fs, dir_handle);
        }

        /* Unlink empty directory */
        (void)i_storage_fs_unlink(&self->fs, target_dir);
        LOG_INF("Pruned oldest logs directory: %s", target_dir);
    }
}

static void datalogger_thread_entry(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    ao_datalogger_t *self = (ao_datalogger_t *)arg1;

    LOG_INF("ao_datalogger thread started (PID %p)", k_current_get());

    while (self->is_running) {
        datalogger_event_envelope_t evt;
        int rc = k_msgq_get(self->queue, &evt, K_MSEC(200));

        if (rc == 0) {
            self->stats.events_received++;

            switch (evt.type) {
            case DATALOGGER_EVT_STOP:
                self->is_running = false;
                break;

            case DATALOGGER_EVT_STORAGE_STATUS:
                k_mutex_lock(&self->lock, K_FOREVER);
                self->is_sd_mounted = (evt.payload.storage_status == STORAGE_FS_CARD_MOUNTED);
                if (self->is_sd_mounted) {
                    char logs_dir[64];
                    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", self->mount_point);
                    ensure_directory_path(self, logs_dir);

                    snprintf(logs_dir, sizeof(logs_dir), "%s/logs/boot", self->mount_point);
                    ensure_directory_path(self, logs_dir);

                    snprintf(logs_dir, sizeof(logs_dir), "%s/logs/%s", self->mount_point, self->current_date);
                    ensure_directory_path(self, logs_dir);

                    reconcile_bkpsram(self);
                    check_storage_low_watermark(self);
                } else {
                    close_stream_file(self, &self->alarms_file);
                    close_stream_file(self, &self->system_file);
                    close_stream_file(self, &self->boot_file);
                }
                k_mutex_unlock(&self->lock);
                break;

            case DATALOGGER_EVT_TIME_SYNC_TRIGGER: {
                k_mutex_lock(&self->lock, K_FOREVER);
                self->is_time_synced = true;
                /* If date is set, ensure calendar directory exists */
                char cal_dir[128];
                snprintf(cal_dir, sizeof(cal_dir), "%s/logs/%s", self->mount_point, self->current_date);
                ensure_directory_path(self, cal_dir);

                /* Close staging file if active */
                close_stream_file(self, &self->boot_file);
                k_mutex_unlock(&self->lock);
                break;
            }

            case DATALOGGER_EVT_ALARM_TRANSITION: {
                alarm_transition_event_t *alm = &evt.payload.alarm;
                char line_buf[256];
                char time_str[32];
                char path_buf[FS_MAX_PATH_LEN];

                k_mutex_lock(&self->lock, K_FOREVER);
                get_iso_timestamp(time_str, sizeof(time_str),
                                  self->is_time_synced ? (int64_t)(alm->timestamp_ms / 1000ULL) : 0,
                                  (uint32_t)(alm->timestamp_ms % 1000ULL));

                snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_alarms.log",
                         self->mount_point, self->current_date, self->current_date);

                int len = snprintf(line_buf, sizeof(line_buf),
                                   "%s,%llu,%u,%u,%s,%s,%.2f,%.2f,%s,%u\n",
                                   time_str,
                                   (unsigned long long)evt.uptime_ms,
                                   (unsigned int)evt.seq_id,
                                   (unsigned int)alm->alarm_id,
                                   subsystem_to_str(alm->source_category),
                                   transition_to_str(alm->event_type),
                                   (double)0.0f,
                                   (double)alm->trigger_value,
                                   severity_to_str(alm->severity),
                                   (unsigned int)alm->alarm_id);

                if (len > 0) {
                    append_to_stream(self, self->alarms_buf, &self->alarms_buf_len,
                                     line_buf, (size_t)len, &self->alarms_file, path_buf, CSV_ALARMS_HEADER);
                    self->stats.events_written++;

                    /* Critical alarms mirrored to BKPSRAM are marked committed upon staging flush */
                    if (alm->severity >= ALARM_SEVERITY_CRITICAL && i_bkpsram_is_bound(&self->bkp)) {
                        (void)i_bkpsram_mark_committed(&self->bkp, evt.seq_id);
                    }
                }
                k_mutex_unlock(&self->lock);
                break;
            }

            case DATALOGGER_EVT_HVAC_EVENT: {
                hvac_event_t *hvac = &evt.payload.hvac;
                char line_buf[256];
                char time_str[32];
                char path_buf[FS_MAX_PATH_LEN];

                k_mutex_lock(&self->lock, K_FOREVER);
                get_iso_timestamp(time_str, sizeof(time_str),
                                  self->is_time_synced ? (int64_t)(hvac->timestamp_ms / 1000U) : 0,
                                  hvac->timestamp_ms % 1000U);

                snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_system.log",
                         self->mount_point, self->current_date, self->current_date);

                int len = snprintf(line_buf, sizeof(line_buf),
                                   "%s [UPTIME:%llu] [SEQ:%u] [INF] [HVAC] Unit %u Group %u Event %u\n",
                                   time_str,
                                   (unsigned long long)evt.uptime_ms,
                                   (unsigned int)evt.seq_id,
                                   (unsigned int)hvac->unit_id,
                                   (unsigned int)hvac->group_id,
                                   (unsigned int)hvac->type);

                if (len > 0) {
                    append_to_stream(self, self->system_buf, &self->system_buf_len,
                                     line_buf, (size_t)len, &self->system_file, path_buf, NULL);
                    self->stats.events_written++;
                }
                k_mutex_unlock(&self->lock);
                break;
            }

            case DATALOGGER_EVT_SYSTEM_EVENT: {
                system_event_msg_t *sys = &evt.payload.system;
                char line_buf[256];
                char time_str[32];
                char path_buf[FS_MAX_PATH_LEN];

                k_mutex_lock(&self->lock, K_FOREVER);
                get_iso_timestamp(time_str, sizeof(time_str),
                                  self->is_time_synced ? (int64_t)(sys->timestamp_ms / 1000ULL) : 0,
                                  (uint32_t)(sys->timestamp_ms % 1000ULL));

                if (!self->is_time_synced) {
                    /* Write to boot staging CSV */
                    snprintf(path_buf, sizeof(path_buf), "%s/logs/boot/boot_%u_staging.csv",
                             self->mount_point, (unsigned int)self->boot_count);
                    int len = snprintf(line_buf, sizeof(line_buf),
                                       "%llu,%u,SYSTEM,%u,%s\n",
                                       (unsigned long long)evt.uptime_ms,
                                       (unsigned int)evt.seq_id,
                                       (unsigned int)sys->type,
                                       sys->message);
                    if (len > 0) {
                        append_to_stream(self, self->boot_buf, &self->boot_buf_len,
                                         line_buf, (size_t)len, &self->boot_file, path_buf, CSV_BOOT_HEADER);
                        self->stats.events_written++;
                    }
                } else {
                    snprintf(path_buf, sizeof(path_buf), "%s/logs/%s/%s_system.log",
                             self->mount_point, self->current_date, self->current_date);
                    int len = snprintf(line_buf, sizeof(line_buf),
                                       "%s [UPTIME:%llu] [SEQ:%u] [%s] [%s] %s\n",
                                       time_str,
                                       (unsigned long long)evt.uptime_ms,
                                       (unsigned int)evt.seq_id,
                                       severity_to_str(sys->severity),
                                       sys->subsystem,
                                       sys->message);
                    if (len > 0) {
                        append_to_stream(self, self->system_buf, &self->system_buf_len,
                                         line_buf, (size_t)len, &self->system_file, path_buf, NULL);
                        self->stats.events_written++;
                    }
                }
                k_mutex_unlock(&self->lock);
                break;
            }

            case DATALOGGER_EVT_FLUSH_TRIGGER:
                k_mutex_lock(&self->lock, K_FOREVER);
                flush_all_streams(self);
                k_mutex_unlock(&self->lock);
                break;

            default:
                break;
            }
        }

        /* Periodic inactivity flush */
        int64_t now = k_uptime_get();
        if ((now - self->last_flush_uptime_ms) >= AO_DATALOGGER_FLUSH_INTERVAL_MS) {
            k_mutex_lock(&self->lock, K_FOREVER);
            flush_all_streams(self);
            k_mutex_unlock(&self->lock);
        }
    }

    /* Thread terminating: final flush & close */
    k_mutex_lock(&self->lock, K_FOREVER);
    flush_all_streams(self);
    close_stream_file(self, &self->alarms_file);
    close_stream_file(self, &self->system_file);
    close_stream_file(self, &self->boot_file);
    k_mutex_unlock(&self->lock);

    k_sem_give(&self->stopped_sem);
    LOG_INF("ao_datalogger thread stopped gracefully");
}

int ao_datalogger_init(ao_datalogger_t *self,
                      const i_storage_fs_t *fs,
                      const i_bkpsram_t *bkp,
                      struct k_msgq *queue,
                      k_thread_stack_t *stack,
                      size_t stack_size,
                      int priority)
{
    if (self == NULL || fs == NULL || queue == NULL || stack == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->fs = *fs;
    if (bkp != NULL) {
        self->bkp = *bkp;
    }
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->priority = priority;
    self->boot_count = 1U;
    self->is_running = false;
    self->is_initialized = false;
    self->is_sd_mounted = false;
    self->is_time_synced = false;
    self->last_flush_uptime_ms = 0;

    strncpy(self->mount_point, "/SD:", sizeof(self->mount_point) - 1U);
    strncpy(self->current_date, "default", sizeof(self->current_date) - 1U);

    k_sem_init(&self->stopped_sem, 0, 1);
    k_mutex_init(&self->lock);

    self->is_initialized = true;
    return 0;
}

int ao_datalogger_start(ao_datalogger_t *self)
{
    if (self == NULL || !self->is_initialized || self->is_running) {
        return -EINVAL;
    }

    self->is_running = true;
    self->last_flush_uptime_ms = k_uptime_get();

    self->tid = k_thread_create(&self->thread,
                                self->stack,
                                self->stack_size,
                                datalogger_thread_entry,
                                self, NULL, NULL,
                                self->priority,
                                0,
                                K_NO_WAIT);

    k_thread_name_set(self->tid, "ao_datalogger");
    return 0;
}

int ao_datalogger_stop(ao_datalogger_t *self)
{
    if (self == NULL || !self->is_running) {
        return -EINVAL;
    }

    datalogger_event_envelope_t stop_evt = {
        .type = DATALOGGER_EVT_STOP,
        .uptime_ms = (uint64_t)k_uptime_get(),
        .seq_id = datalogger_next_seq_id(),
    };

    return k_msgq_put(self->queue, &stop_evt, K_MSEC(100));
}

int ao_datalogger_wait_stopped(ao_datalogger_t *self, k_timeout_t timeout)
{
    if (self == NULL) {
        return -EINVAL;
    }
    int rc = k_sem_take(&self->stopped_sem, timeout);
    if (rc == 0 && self->tid != NULL) {
        (void)k_thread_join(self->tid, timeout);
        self->tid = NULL;
    }
    return rc;
}

int ao_datalogger_deinit(ao_datalogger_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    if (self->is_running) {
        (void)ao_datalogger_stop(self);
        int rc = ao_datalogger_wait_stopped(self, K_MSEC(500));
        if (rc != 0 && self->tid != NULL) {
            k_thread_abort(self->tid);
            self->tid = NULL;
        }
    } else if (self->tid != NULL) {
        (void)k_thread_join(self->tid, K_MSEC(200));
        self->tid = NULL;
    }

    self->is_running = false;
    self->is_initialized = false;
    return 0;
}

int ao_datalogger_post_event(ao_datalogger_t *self, const datalogger_event_envelope_t *evt)
{
    if (self == NULL || !self->is_initialized || evt == NULL) {
        return -EINVAL;
    }

    int rc = k_msgq_put(self->queue, evt, K_NO_WAIT);
    if (rc != 0) {
        self->stats.events_dropped++;
    }
    return rc;
}

int ao_datalogger_get_stats(ao_datalogger_t *self, ao_datalogger_stats_t *out_stats)
{
    if (self == NULL || out_stats == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    memcpy(out_stats, &self->stats, sizeof(ao_datalogger_stats_t));
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_datalogger_set_date(ao_datalogger_t *self, const char *date_str)
{
    if (self == NULL || date_str == NULL || strlen(date_str) == 0) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    strncpy(self->current_date, date_str, sizeof(self->current_date) - 1U);
    self->current_date[sizeof(self->current_date) - 1U] = '\0';
    self->is_time_synced = true;

    /* Ensure daily directory exists if SD is mounted */
    if (self->is_sd_mounted) {
        char daily_dir[128];
        snprintf(daily_dir, sizeof(daily_dir), "%s/logs/%s", self->mount_point, self->current_date);
        ensure_directory_path(self, daily_dir);
        close_stream_file(self, &self->boot_file);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_datalogger_flush(ao_datalogger_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    datalogger_event_envelope_t flush_evt = {
        .type = DATALOGGER_EVT_FLUSH_TRIGGER,
        .uptime_ms = (uint64_t)k_uptime_get(),
        .seq_id = datalogger_next_seq_id(),
    };

    return k_msgq_put(self->queue, &flush_evt, K_MSEC(50));
}
