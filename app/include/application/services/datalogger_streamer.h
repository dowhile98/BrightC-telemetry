/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file datalogger_streamer.h
 * @brief Chunked HTTP/REST Streaming Service for Historical Telemetry and Logs.
 *
 * Provides zero-copy, bounded 2 KB chunk streaming directly from MicroSD FatFS
 * clusters to network consumers, avoiding heap fragmentation and large RAM allocations.
 */

#ifndef APP_APPLICATION_SERVICES_DATALOGGER_STREAMER_H_
#define APP_APPLICATION_SERVICES_DATALOGGER_STREAMER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_storage_fs.h"
#include "application/active_objects/ao_datalogger.h"
#include "common/datalogger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATALOGGER_STREAMER_CHUNK_SIZE 1024U

/**
 * @brief Sink callback invoked for each chunk read from storage.
 * @return 0 to continue streaming, non-zero to abort transfer early.
 */
typedef int (*datalogger_stream_sink_cb_t)(const uint8_t *chunk, size_t len, void *user_data);

/**
 * @brief Datalogger chunked streamer service context.
 */
typedef struct {
    i_storage_fs_t   fs;
    ao_datalogger_t *datalogger_ao;
    char             mount_point[32];
    bool             is_initialized;
} datalogger_streamer_t;

/**
 * @brief Initialize the historical datalogger streamer service.
 *
 * @param[in,out] self          Streamer context.
 * @param[in]     fs            Abstract filesystem port.
 * @param[in]     datalogger_ao Active Object context (used for active-file synchronization).
 * @param[in]     mount_point   Root storage mount path (e.g. "/SD:").
 * @return 0 on success, negative errno on failure.
 */
int datalogger_streamer_init(datalogger_streamer_t *self,
                             const i_storage_fs_t *fs,
                             ao_datalogger_t *datalogger_ao,
                             const char *mount_point);

/**
 * @brief Validate whether a date string conforms strictly to "YYYY-MM-DD".
 * Rejects path traversal and illegal characters.
 *
 * @param[in] date_str Date string to validate.
 * @return true if valid "YYYY-MM-DD", false otherwise.
 */
bool datalogger_streamer_is_valid_date(const char *date_str);

/**
 * @brief Stream a log or history file in 2 KB chunks to the provided sink callback.
 *
 * @param[in,out] self         Streamer context.
 * @param[in]     stream_type  LOG_STREAM_TELEMETRY, LOG_STREAM_ALARMS, or LOG_STREAM_SYSTEM.
 * @param[in]     date_str     Calendar date string ("YYYY-MM-DD").
 * @param[in]     sink_cb      Callback invoked for each chunk.
 * @param[in]     user_data    User context passed to sink_cb.
 * @param[out]    bytes_sent   Optional counter of total bytes successfully delivered.
 * @return 0 on complete delivery, negative errno on failure.
 */
int datalogger_streamer_stream_file(datalogger_streamer_t *self,
                                    log_stream_t stream_type,
                                    const char *date_str,
                                    datalogger_stream_sink_cb_t sink_cb,
                                    void *user_data,
                                    size_t *bytes_sent);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_SERVICES_DATALOGGER_STREAMER_H_ */
