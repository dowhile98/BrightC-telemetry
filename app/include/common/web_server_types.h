/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, DTOs, and configuration definitions
 * for the Embedded Web Server & REST API subsystem on BrightC Controller V3.
 *
 * Pure C99/C11 - zero Zephyr headers, zero vendor HAL, zero dynamic memory.
 */

#ifndef APP_COMMON_WEB_SERVER_TYPES_H_
#define APP_COMMON_WEB_SERVER_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum string lengths */
#define WEB_SERVER_MAX_PATH_LEN         128
#define WEB_SERVER_MAX_URI_LEN          128
#define WEB_SERVER_MAX_CONTENT_TYPE_LEN  48
#define WEB_SERVER_DATE_STR_LEN          16

/*============================================================================*
 * 1. HTTP PROTOCOL ENUMS & STATUS
 *============================================================================*/

/**
 * @brief Runtime operational status of the embedded HTTP server.
 */
typedef enum {
    WEB_SERVER_STATUS_STOPPED = 0,         /**< Server thread is stopped / inactive */
    WEB_SERVER_STATUS_STARTING,            /**< Sockets binding and initialization */
    WEB_SERVER_STATUS_RUNNING,             /**< Normal operational state with SD mounted */
    WEB_SERVER_STATUS_DEGRADED_NO_SD,      /**< Running in degraded mode (Flash fallback / 503) */
    WEB_SERVER_STATUS_ERROR,               /**< Fatal network or socket binding error */
} web_server_status_t;

/**
 * @brief Bitmask flags of HTTP methods supported by resources.
 */
typedef enum {
    WEB_HTTP_METHOD_GET     = (1U << 0),   /**< HTTP GET */
    WEB_HTTP_METHOD_POST    = (1U << 1),   /**< HTTP POST */
    WEB_HTTP_METHOD_PUT     = (1U << 2),   /**< HTTP PUT */
    WEB_HTTP_METHOD_DELETE  = (1U << 3),   /**< HTTP DELETE */
    WEB_HTTP_METHOD_HEAD    = (1U << 4),   /**< HTTP HEAD */
    WEB_HTTP_METHOD_OPTIONS = (1U << 5),   /**< HTTP OPTIONS */
} web_http_method_t;

/**
 * @brief Common HTTP MIME content types.
 */
typedef enum {
    WEB_CONTENT_TYPE_HTML = 0,             /**< text/html */
    WEB_CONTENT_TYPE_CSS,                  /**< text/css */
    WEB_CONTENT_TYPE_JS,                   /**< text/javascript */
    WEB_CONTENT_TYPE_JSON,                 /**< application/json */
    WEB_CONTENT_TYPE_CSV,                  /**< text/csv; charset=utf-8 */
    WEB_CONTENT_TYPE_SVG,                  /**< image/svg+xml */
    WEB_CONTENT_TYPE_PNG,                  /**< image/png */
    WEB_CONTENT_TYPE_OCTET_STREAM,         /**< application/octet-stream */
} web_content_type_t;

/**
 * @brief Historical telemetry stream category.
 */
typedef enum {
    WEB_HISTORY_STREAM_CONTROL = 0,        /**< HVAC control & relay events */
    WEB_HISTORY_STREAM_ENERGY,             /**< 3-phase electrical energy */
    WEB_HISTORY_STREAM_THERMAL,            /**< Rev F thermodynamic energy balance */
} web_history_stream_type_t;

/*============================================================================*
 * 2. RUNTIME CONFIGURATION & METRICS
 *============================================================================*/

/**
 * @brief Static configuration for the HTTP server instance.
 */
typedef struct {
    uint16_t port;                         /**< Listening TCP port (default: 80) */
    uint16_t max_concurrent_clients;       /**< Concurrent client sockets (1..4) */
    uint32_t client_timeout_sec;           /**< Socket read/write inactivity timeout */
    char root_fs_path[WEB_SERVER_MAX_PATH_LEN]; /**< Root filesystem mount point (e.g. "/SD:/www") */
    bool enable_compression;               /**< Enable automatic .gz search and Content-Encoding */
    bool enable_flash_fallback;            /**< Serve internal flash page if SD is missing */
} web_server_config_t;

/**
 * @brief Cumulative runtime metrics and health telemetry.
 */
typedef struct {
    web_server_status_t status;            /**< Current server status */
    uint16_t active_connections;           /**< Currently connected client sockets */
    uint32_t total_requests;               /**< Cumulative HTTP requests serviced */
    uint32_t get_requests;                 /**< Total GET requests */
    uint32_t post_requests;                /**< Total POST requests */
    uint32_t bytes_sent;                   /**< Total HTTP response bytes transmitted */
    uint32_t bytes_received;               /**< Total HTTP request bytes received */
    uint32_t error_400_count;              /**< Bad Request responses */
    uint32_t error_404_count;              /**< Not Found responses */
    uint32_t error_500_count;              /**< Internal Server Error / 503 responses */
    uint32_t uptime_seconds;               /**< Server thread uptime in seconds */
    bool is_sd_mounted;                    /**< True if MicroSD card is mounted and accessible */
} web_server_metrics_t;

/*============================================================================*
 * 3. REST API DATA TRANSFER OBJECTS (DTOs)
 *============================================================================*/

/**
 * @brief DTO for GET /api/hvac/status
 */
typedef struct {
    uint8_t system_mode;                   /**< 0: OFF, 1: MANUAL, 2: AUTO, 3: FAILSAFE */
    uint8_t active_units_mask;             /**< Bitmask of energized compressors/stages (0..7) */
    uint8_t duty_rotation_index;           /**< Current lead unit index in rotation */
    int16_t room_temp_c_x100;              /**< Average conditioned room temperature (°C * 100) */
    int16_t return_temp_c_x100;            /**< Return air temperature (°C * 100) */
    int16_t setpoint_c_x100;               /**< Active setpoint temperature (°C * 100) */
    uint32_t stage_run_seconds[8];         /**< Cumulative runtime seconds per HVAC stage */
    uint32_t alarm_flags;                  /**< Active HVAC alarm bitmask */
} rest_hvac_status_dto_t;

/**
 * @brief DTO for POST /api/hvac/setpoint
 */
typedef struct {
    int16_t target_temp_c_x100;            /**< Desired target setpoint (°C * 100) */
    int16_t hysteresis_c_x100;             /**< Switching deadband hysteresis (°C * 100) */
    uint16_t stage_delay_sec;              /**< Minimum delay between staging compressors */
    int16_t high_temp_alarm_c_x100;        /**< High room temp alarm threshold (°C * 100) */
} rest_hvac_setpoint_dto_t;

/**
 * @brief DTO for GET /api/energy/status
 */
typedef struct {
    uint32_t voltage_rms_mv[3];            /**< Phase-to-neutral voltages Phase A/B/C (mV) */
    uint32_t current_rms_ma[3];            /**< Phase currents Phase A/B/C (mA) */
    int32_t active_power_w;                /**< Total 3-phase real power (W) */
    int32_t reactive_power_var;            /**< Total 3-phase reactive power (VAR) */
    int16_t power_factor_x1000;            /**< Total power factor (-1000..1000) */
    uint32_t frequency_hz_x100;            /**< Grid frequency (Hz * 100) */
    uint64_t total_energy_wh;              /**< Cumulative active energy consumption (Wh) */
} rest_energy_status_dto_t;

/**
 * @brief DTO for GET /api/thermal/status
 */
typedef struct {
    int32_t q_sensible_w;                  /**< Sensible cooling capacity (W) */
    int32_t q_latent_w;                    /**< Latent cooling capacity (W) */
    int32_t q_total_w;                     /**< Total cooling capacity (W) */
    uint16_t cop_x100;                     /**< Coefficient of Performance (COP * 100) */
    int16_t delta_t_c_x100;                /**< Air temp drop through coil (°C * 100) */
    uint32_t airflow_m3h;                  /**< Total airflow rate (m³/h) */
    int16_t dew_point_c_x100;              /**< Conditioned air dew point (°C * 100) */
    uint8_t thermodynamic_state;           /**< Model calculation quality / state */
} rest_thermal_status_dto_t;

/**
 * @brief DTO for POST /api/alarm/ack
 */
typedef struct {
    uint32_t alarm_id;                     /**< Identifier of alarm being acknowledged */
    bool mute_buzzer;                      /**< If true, mute the active audible alarm melody */
    uint32_t ack_timestamp_utc;            /**< UTC epoch timestamp of acknowledgement */
} rest_alarm_ack_dto_t;

/**
 * @brief DTO for query parameters in GET /api/history/download
 */
typedef struct {
    web_history_stream_type_t stream_type; /**< Stream type: control, energy, thermal */
    char date_str[WEB_SERVER_DATE_STR_LEN];/**< Target date formatted as "YYYY-MM-DD" */
} rest_history_query_dto_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_WEB_SERVER_TYPES_H_ */
