/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file modbus_tcp_types.h
 * @brief Common types, constant bounds, register addresses and data structures
 *        for the Industrial Modbus TCP Server Subsystem.
 *
 * Pure ISO C99/C11 declarations:
 * - Zero Zephyr RTOS dependencies
 * - Zero vendor HAL or hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_COMMON_MODBUS_TCP_TYPES_H_
#define APP_COMMON_MODBUS_TCP_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Capacity, Connection Bounds & Protocol Defaults                           */
/* ========================================================================= */

/** Standard Modbus TCP listening port (IANA assigned). */
#define MODBUS_TCP_DEFAULT_PORT                  502U

/** Default maximum concurrent Modbus TCP client connections. */
#define MODBUS_TCP_MAX_CLIENTS                   4U
#define MODBUS_TCP_MAX_CLIENTS_DEFAULT           4U

/** Absolute maximum concurrent Modbus TCP client connections supported in RAM. */
#define MODBUS_TCP_MAX_CLIENTS_LIMIT             8U

/** Inactivity connection drop timeout (milliseconds) - 30 seconds. */
#define MODBUS_TCP_INACTIVITY_TIMEOUT_MS_DEFAULT 30000U
#define MODBUS_TCP_INACTIVITY_TIMEOUT_MS         30000U

/** Default socket listen backlog. */
#define MODBUS_TCP_LISTEN_BACKLOG_DEFAULT        4U

/** Modbus TCP ADU Maximum Header Size (MBAP 7 bytes + PDU 253 bytes). */
#define MODBUS_TCP_MAX_ADU_LENGTH                260U

/** Dedicated thread stack size and priority. */
#define MODBUS_TCP_THREAD_STACK_SIZE             3072U
#define MODBUS_TCP_THREAD_PRIORITY               8

/* ========================================================================= */
/* Operational States & Enums                                                */
/* ========================================================================= */

/**
 * @brief Modbus TCP Server lifecycle states.
 */
typedef enum {
    MODBUS_TCP_STATE_UNINITIALIZED = 0, /**< Server structure not yet initialized */
    MODBUS_TCP_STATE_STOPPED,           /**< Initialized, sockets closed, dormant */
    MODBUS_TCP_STATE_STARTING,          /**< Creating and binding listening socket */
    MODBUS_TCP_STATE_LISTENING,         /**< Actively polling connections and serving clients */
    MODBUS_TCP_STATE_ERROR              /**< Fatal socket error encountered */
} modbus_tcp_server_state_t;

/**
 * @brief Session state for an individual pooled client slot.
 */
typedef enum {
    MODBUS_TCP_SESSION_IDLE = 0,        /**< Slot available for incoming connection */
    MODBUS_TCP_SESSION_CONNECTED,       /**< TCP connection established; waiting for PDU */
    MODBUS_TCP_SESSION_PROCESSING,      /**< Decoding/executing Modbus callback */
    MODBUS_TCP_SESSION_CLOSING          /**< Session timed out or peer closed connection */
} modbus_tcp_session_state_t;

/**
 * @brief Word endianness representation for 32-bit registers (Floats, uint32_t).
 */
typedef enum {
    MODBUS_WORD_ORDER_BIG_ENDIAN = 0,   /**< AB CD (High Word first, standard Modbus) */
    MODBUS_WORD_ORDER_LITTLE_ENDIAN,    /**< CD AB (Low Word first, word-swapped) */
} modbus_word_order_t;

/**
 * @brief Supervisory command opcodes executed via holding registers 40201..40205.
 */
typedef enum {
    MODBUS_SUPERVISORY_CMD_NONE                 = 0, /**< No operation / Idle */
    MODBUS_SUPERVISORY_CMD_ACK_ALARM_ID         = 1, /**< Acknowledge alarm by ID (Arg = Alarm ID) */
    MODBUS_SUPERVISORY_CMD_ACK_ALL_ALARMS       = 2, /**< Acknowledge all active alarms */
    MODBUS_SUPERVISORY_CMD_RESET_LATCH          = 3, /**< Reset latched alarm (Arg = ID, 0 = all) */
    MODBUS_SUPERVISORY_CMD_REBOOT               = 4, /**< Supervised controller reboot */
    MODBUS_SUPERVISORY_CMD_STORAGE_SAVE_ALL     = 5, /**< Save all dirty storage domains */
    MODBUS_SUPERVISORY_CMD_RECALIBRATE_BASELINE = 6, /**< Recalibrate steady-state thermal baseline */
} modbus_supervisory_cmd_t;

/* ========================================================================= */
/* Statistics & Diagnostic Structures                                        */
/* ========================================================================= */

/**
 * @brief Cumulative operational metrics for Modbus TCP server diagnostics.
 */
typedef struct {
    uint32_t rx_frames;                  /**< Total Modbus TCP frames received */
    uint32_t tx_frames;                  /**< Total Modbus TCP frames transmitted */
    uint32_t crc_errors;                 /**< Total CRC errors detected */
    uint32_t exceptions;                 /**< Total Modbus exceptions generated/sent */
    uint32_t timeouts;                   /**< Total frame receive timeouts */
} modbus_tcp_server_stats_t;

/**
 * @brief Detailed status information of an active client session.
 */
typedef struct {
    uint8_t                    slot_index;               /**< Session pool slot index (0..max_clients-1) */
    modbus_tcp_session_state_t state;                    /**< Current session state */
    int                        sock_fd;                  /**< Active BSD socket file descriptor */
    uint32_t                   client_ip_be;             /**< Client IPv4 address in network byte order */
    uint16_t                   client_port;              /**< Client ephemeral TCP port */
    uint64_t                   connected_uptime_ms;      /**< Uptime timestamp when connection was accepted */
    uint64_t                   last_activity_uptime_ms;  /**< Uptime timestamp of last read/write transaction */
    uint32_t                   requests_served;          /**< Total valid PDUs processed for this session */
    uint32_t                   exceptions_returned;      /**< Exceptions returned to this client */
} modbus_tcp_session_info_t;

/**
 * @brief Modbus TCP Server configuration parameters.
 */
typedef struct {
    uint16_t            port;                   /**< TCP Port (default 502) */
    uint8_t             max_clients;            /**< Max concurrent clients (1..8, default 4) */
    uint32_t            inactivity_timeout_ms;  /**< Disconnect client after inactivity (default 30000 ms) */
    modbus_word_order_t word_order;             /**< 32-bit register word packing endianness */
} modbus_tcp_server_config_t;

/* ========================================================================= */
/* Register Map PDU Offsets and Spans                                        */
/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/* 1. DISCRETE INPUTS (Function Code 0x02, Read-Only, 1-bit)                 */
/* ------------------------------------------------------------------------- */

/* Section 1.1: Digital Inputs (Modbus Ref 10001..10056) */
#define MODBUS_DI_OFFSET_DISCRETE_INPUTS         0x0000U /**< PDU Offset for DI 0 */
#define MODBUS_DI_COUNT_DISCRETE_INPUTS          56U     /**< 56 physical digital inputs */

/* Section 1.2: Active Alarm States (Modbus Ref 10101..10180) */
#define MODBUS_DI_OFFSET_ALARMS                  0x0064U /**< PDU Offset 100 (10101 - 10001) */
#define MODBUS_DI_COUNT_ALARMS                   80U     /**< 79 catalog alarms + sentinel */

/* Section 1.3: HVAC Units Tri-State Flags (Modbus Ref 10201..10224) */
#define MODBUS_DI_OFFSET_HVAC_TRISTATE           0x00C8U /**< PDU Offset 200 (10201 - 10001) */
#define MODBUS_DI_COUNT_HVAC_TRISTATE            24U     /**< 8 units * 3 relays (AA, SM, G) */

/* ------------------------------------------------------------------------- */
/* 2. COILS (Function Codes 0x01, 0x05, 0x0F, Read/Write, 1-bit)             */
/* ------------------------------------------------------------------------- */

/* Section 2.1: HVAC Units Relays AA/SM/G Manual Overrides (Modbus Ref 00001..00024) */
#define MODBUS_COIL_OFFSET_HVAC_OVERRIDES        0x0000U /**< PDU Offset for Unit 0 Relay AA */
#define MODBUS_COIL_COUNT_HVAC_OVERRIDES         24U     /**< 8 units * 3 relays (AA, SM, G) */

/* Section 2.2: Alarm Buzzer Mute Toggle (Modbus Ref 00050) */
#define MODBUS_COIL_OFFSET_BUZZER_MUTE           0x0031U /**< PDU Offset 49 (50 - 1) */

/* Section 2.3: Group Force Rotation Pulses (Modbus Ref 00060..00063) */
#define MODBUS_COIL_OFFSET_FORCE_ROTATION        0x003BU /**< PDU Offset 59 (60 - 1) */
#define MODBUS_COIL_COUNT_FORCE_ROTATION         4U      /**< 4 HVAC Groups (0..3) */

/* Section 2.4: Storage Save All Trigger (Modbus Ref 00070) */
#define MODBUS_COIL_OFFSET_STORAGE_SAVE_ALL      0x0045U /**< PDU Offset 69 (70 - 1) */

/* ------------------------------------------------------------------------- */
/* 3. INPUT REGISTERS (Function Code 0x04, Read-Only, 16-bit)                */
/* ------------------------------------------------------------------------- */

/* Section 3.1: Sensor Matrix Temperatures in 0.1 °C (Modbus Ref 30001..30064) */
#define MODBUS_IR_OFFSET_SENSOR_MATRIX           0x0000U /**< PDU Offset for Channel 0 */
#define MODBUS_IR_COUNT_SENSOR_MATRIX            64U     /**< 64 Sensor Matrix Temperatures */

/* Section 3.2: Return RH %x10 and Valid Flag (Modbus Ref 30070..30071) */
#define MODBUS_IR_OFFSET_RETURN_RH               0x0045U /**< PDU Offset 69 (30070 - 30001) */
#define MODBUS_IR_COUNT_RETURN_RH                2U      /**< 30070: RH x10, 30071: Valid flag */

/* Section 3.3: 3-Phase Electrical Parameters (Modbus Ref 30101..30130) */
#define MODBUS_IR_OFFSET_ELECTRICAL              0x0064U /**< PDU Offset 100 (30101 - 30001) */
#define MODBUS_IR_COUNT_ELECTRICAL               30U     /**< V, I, kW, PF, Hz, kWh */

/* Section 3.4: Thermodynamic Metrics (Modbus Ref 30201..30230) */
#define MODBUS_IR_OFFSET_THERMODYNAMIC           0x00C8U /**< PDU Offset 200 (30201 - 30001) */
#define MODBUS_IR_COUNT_THERMODYNAMIC            30U     /**< COP, Q_sens, Q_lat, Q_tot, eta_ii, airflow */

/* Section 3.5: System Uptime / IP / MicroSD (Modbus Ref 30301..30320) */
#define MODBUS_IR_OFFSET_SYSTEM_DIAG             0x012CU /**< PDU Offset 300 (30301 - 30001) */
#define MODBUS_IR_COUNT_SYSTEM_DIAG              20U     /**< Uptime, Eth IP, WiFi IP, SD total & free */

/* Section 3.6: Active Alarm Diagnostics (Modbus Ref 30401..30410) */
#define MODBUS_IR_OFFSET_ALARM_DIAG              0x0190U /**< PDU Offset 400 (30401 - 30001) */
#define MODBUS_IR_COUNT_ALARM_DIAG               10U     /**< Active count, unack, severity, masks */

/* ------------------------------------------------------------------------- */
/* 4. HOLDING REGISTERS (Function Codes 0x03, 0x06, 0x10, Read/Write 16-bit)  */
/* ------------------------------------------------------------------------- */

/* Section 4.1: HVAC Groups 0..3 Setpoints Tmin/Tmax/Talm/Tbyp (Modbus Ref 40001..40040) */
#define MODBUS_HR_OFFSET_HVAC_SETPOINTS          0x0000U /**< PDU Offset 0 */
#define MODBUS_HR_COUNT_HVAC_SETPOINTS           40U     /**< 4 Groups * 10 registers */

/* Section 4.2: HVAC Groups 0..3 Configuration (Modbus Ref 40041..40060) */
#define MODBUS_HR_OFFSET_HVAC_CONFIG             0x0028U /**< PDU Offset 40 (40041 - 40001) */
#define MODBUS_HR_COUNT_HVAC_CONFIG              20U     /**< 4 Groups * 5 registers */

/* Section 4.3: RTC Calendar & UNIX Epoch (Modbus Ref 40101..40110) */
#define MODBUS_HR_OFFSET_RTC                     0x0064U /**< PDU Offset 100 (40101 - 40001) */
#define MODBUS_HR_COUNT_RTC                      10U     /**< Year, Mon, Day, Hour, Min, Sec, Epoch */

/* Section 4.4: Supervisory Command Execution (Modbus Ref 40201..40205) */
#define MODBUS_HR_OFFSET_SUPERVISORY             0x00C8U /**< PDU Offset 200 (40201 - 40001) */
#define MODBUS_HR_COUNT_SUPERVISORY              5U      /**< Opcode, Arg, Status, LastOp, LastErr */

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_TCP_TYPES_H_ */
