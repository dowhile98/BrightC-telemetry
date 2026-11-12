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
#define MODBUS_TCP_MAX_CLIENTS_DEFAULT           4U

/** Absolute maximum concurrent Modbus TCP client connections supported in RAM. */
#define MODBUS_TCP_MAX_CLIENTS_LIMIT             8U

/** Inactivity connection drop timeout (milliseconds) - 30 seconds. */
#define MODBUS_TCP_INACTIVITY_TIMEOUT_MS_DEFAULT 30000U

/** Socket receive timeout per non-blocking poll cycle (milliseconds). */
#define MODBUS_TCP_RX_TIMEOUT_MS_DEFAULT         1000U

/** Socket listen backlog size. */
#define MODBUS_TCP_LISTEN_BACKLOG_DEFAULT        4U

/** Modbus TCP ADU Maximum Header Size (MBAP 7 bytes + PDU 253 bytes). */
#define MODBUS_TCP_MAX_ADU_LENGTH                260U

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

/* ========================================================================= */
/* Statistics & Diagnostic Structures                                        */
/* ========================================================================= */

/**
 * @brief Cumulative operational metrics for Modbus TCP server diagnostics.
 */
typedef struct {
    uint32_t total_connections_accepted; /**< Total TCP clients accepted since boot */
    uint32_t active_clients_count;       /**< Number of currently established TCP clients */
    uint32_t rx_frames;                  /**< Valid Modbus TCP request ADUs processed */
    uint32_t tx_frames;                  /**< Modbus TCP response ADUs transmitted */
    uint32_t exceptions_sent;            /**< Modbus exception frames returned (0x80 + FC) */
    uint32_t inactivity_timeouts;        /**< Clients dropped due to inactivity threshold */
    uint32_t conn_rejected_pool_full;    /**< Connections refused because pool was exhausted */
    uint32_t socket_errors;              /**< Transport-level read/write/poll socket errors */
} modbus_tcp_server_stats_t;

/**
 * @brief Detailed status information of an active client session.
 */
typedef struct {
    uint8_t  slot_index;                 /**< Session pool slot index (0..max_clients-1) */
    modbus_tcp_session_state_t state;    /**< Current session state */
    int      sock_fd;                    /**< Active BSD socket file descriptor */
    uint32_t client_ip_be;               /**< Client IPv4 address in network byte order */
    uint16_t client_port;                /**< Client remote TCP port */
    uint64_t connected_at_uptime_ms;     /**< Monotonic timestamp when connected */
    uint64_t last_activity_uptime_ms;    /**< Monotonic timestamp of last received PDU */
    uint32_t requests_served;            /**< Requests processed for this specific session */
} modbus_tcp_session_info_t;

/**
 * @brief Runtime configuration for the Modbus TCP Server adapter.
 */
typedef struct {
    uint16_t            port;                  /**< Listening TCP port (default 502) */
    uint8_t             max_clients;           /**< Max simultaneous clients (1..MODBUS_TCP_MAX_CLIENTS_LIMIT) */
    uint32_t            inactivity_timeout_ms; /**< Client disconnect timeout (default 30000 ms) */
    modbus_word_order_t word_order;            /**< 32-bit register word arrangement */
    bool                allow_write_coils;     /**< Permission flag for supervisory coils */
    bool                allow_write_holding;   /**< Permission flag for holding register writes */
} modbus_tcp_server_config_t;

/* ========================================================================= */
/* Modbus Register Map Address Definitions                                   */
/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/* 1. DISCRETE INPUTS (Function Code 0x02, Read-Only 1-bit, Base 10001+)     */
/* ------------------------------------------------------------------------- */

/* PDU Offset Range: 0x0000 .. 0x000F (Addresses: 10001 .. 10016) - Sala/Group Alarms */
#define MODBUS_DI_ADDR_G1_TEMP_HIGH          10001U /**< PDU 0x0000: Group 1 High Temperature Alarm */
#define MODBUS_DI_ADDR_G1_CRITICAL_BYPASS    10002U /**< PDU 0x0001: Group 1 Critical Thermal Bypass Active */
#define MODBUS_DI_ADDR_G1_TEMP_LOW           10003U /**< PDU 0x0002: Group 1 Low Temperature Alarm */
#define MODBUS_DI_ADDR_G1_SENSOR_FAULT       10004U /**< PDU 0x0003: Group 1 Sensor / Probe Failure */

#define MODBUS_DI_ADDR_G2_TEMP_HIGH          10005U /**< PDU 0x0004: Group 2 High Temperature Alarm */
#define MODBUS_DI_ADDR_G2_CRITICAL_BYPASS    10006U /**< PDU 0x0005: Group 2 Critical Thermal Bypass Active */
#define MODBUS_DI_ADDR_G2_TEMP_LOW           10007U /**< PDU 0x0006: Group 2 Low Temperature Alarm */
#define MODBUS_DI_ADDR_G2_SENSOR_FAULT       10008U /**< PDU 0x0007: Group 2 Sensor / Probe Failure */

#define MODBUS_DI_ADDR_G3_TEMP_HIGH          10009U /**< PDU 0x0008: Group 3 High Temperature Alarm */
#define MODBUS_DI_ADDR_G3_CRITICAL_BYPASS    10010U /**< PDU 0x0009: Group 3 Critical Thermal Bypass Active */
#define MODBUS_DI_ADDR_G3_TEMP_LOW           10011U /**< PDU 0x000A: Group 3 Low Temperature Alarm */
#define MODBUS_DI_ADDR_G3_SENSOR_FAULT       10012U /**< PDU 0x000B: Group 3 Sensor / Probe Failure */

#define MODBUS_DI_ADDR_G4_TEMP_HIGH          10013U /**< PDU 0x000C: Group 4 High Temperature Alarm */
#define MODBUS_DI_ADDR_G4_CRITICAL_BYPASS    10014U /**< PDU 0x000D: Group 4 Critical Thermal Bypass Active */
#define MODBUS_DI_ADDR_G4_TEMP_LOW           10015U /**< PDU 0x000E: Group 4 Low Temperature Alarm */
#define MODBUS_DI_ADDR_G4_SENSOR_FAULT       10016U /**< PDU 0x000F: Group 4 Sensor / Probe Failure */

/* PDU Offset Range: 0x0013 .. 0x001A (Addresses: 10020 .. 10027) - AC1..8 Summary Alarms */
#define MODBUS_DI_ADDR_AC1_SUMMARY_FAULT     10020U /**< PDU 0x0013: AC1 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC2_SUMMARY_FAULT     10021U /**< PDU 0x0014: AC2 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC3_SUMMARY_FAULT     10022U /**< PDU 0x0015: AC3 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC4_SUMMARY_FAULT     10023U /**< PDU 0x0016: AC4 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC5_SUMMARY_FAULT     10024U /**< PDU 0x0017: AC5 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC6_SUMMARY_FAULT     10025U /**< PDU 0x0018: AC6 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC7_SUMMARY_FAULT     10026U /**< PDU 0x0019: AC7 Consolidated Summary Fault */
#define MODBUS_DI_ADDR_AC8_SUMMARY_FAULT     10027U /**< PDU 0x001A: AC8 Consolidated Summary Fault */

/* PDU Offset Range: 0x001D .. 0x002C (Addresses: 10030 .. 10045) - Physical & Infra Inputs */
#define MODBUS_DI_ADDR_FACILITY_FIRE         10030U /**< PDU 0x001D: Smoke / Fire Detection Contact */
#define MODBUS_DI_ADDR_FACILITY_WATER_LEAK   10031U /**< PDU 0x001E: Water Leak / Flood Detection */
#define MODBUS_DI_ADDR_FACILITY_PHASE_MON    10032U /**< PDU 0x001F: 3-Phase Monitor Loss / Under-Voltage */
#define MODBUS_DI_ADDR_FACILITY_DOOR_OPEN    10033U /**< PDU 0x0020: Enclosure / Facility Door Open Contact */
#define MODBUS_DI_ADDR_FACILITY_SURGE_PROT   10034U /**< PDU 0x0021: Surge Protection Device (SPD) Trip */
#define MODBUS_DI_ADDR_EMERGENCY_STOP        10035U /**< PDU 0x0022: Manual Emergency Stop Active */
#define MODBUS_DI_ADDR_SD_CARD_FAULT         10036U /**< PDU 0x0023: MicroSD Storage Mount / I/O Fault */
#define MODBUS_DI_ADDR_MODBUS_BUS1_COMM_LOSS 10037U /**< PDU 0x0024: Modbus RS485-1 Bus Offline / Comm Loss */
#define MODBUS_DI_ADDR_MODBUS_BUS2_COMM_LOSS 10038U /**< PDU 0x0025: Modbus RS485-2 Bus Offline / Comm Loss */
#define MODBUS_DI_ADDR_ENERGY_METER_COMM_LOSS 10039U /**< PDU 0x0026: Energy Meter JSY-MK-339 Comm Loss */
#define MODBUS_DI_ADDR_ETHERNET_LINK_DOWN    10040U /**< PDU 0x0027: Ethernet Physical Cable Unplugged */
#define MODBUS_DI_ADDR_WIFI_DISCONNECTED     10041U /**< PDU 0x0028: Wi-Fi STA Link Disconnected */
#define MODBUS_DI_ADDR_RTC_BATTERY_LOW       10042U /**< PDU 0x0029: Real-Time Clock Backup Battery Low */
#define MODBUS_DI_ADDR_FLASH_NVS_FAULT       10043U /**< PDU 0x002A: Internal Flash / NVS Storage Degraded */
#define MODBUS_DI_ADDR_RESERVED_10044        10044U /**< PDU 0x002B: Reserved */
#define MODBUS_DI_ADDR_RESERVED_10045        10045U /**< PDU 0x002C: Reserved */

/* PDU Offset Range: 0x0031 .. 0x0038 (Addresses: 10050 .. 10057) - AC1..8 Running Feedback */
#define MODBUS_DI_ADDR_AC1_RUN_FEEDBACK      10050U /**< PDU 0x0031: AC1 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC2_RUN_FEEDBACK      10051U /**< PDU 0x0032: AC2 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC3_RUN_FEEDBACK      10052U /**< PDU 0x0033: AC3 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC4_RUN_FEEDBACK      10053U /**< PDU 0x0034: AC4 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC5_RUN_FEEDBACK      10054U /**< PDU 0x0035: AC5 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC6_RUN_FEEDBACK      10055U /**< PDU 0x0036: AC6 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC7_RUN_FEEDBACK      10056U /**< PDU 0x0037: AC7 Auxiliary Run Feedback Contact */
#define MODBUS_DI_ADDR_AC8_RUN_FEEDBACK      10057U /**< PDU 0x0038: AC8 Auxiliary Run Feedback Contact */

/* ------------------------------------------------------------------------- */
/* 2. COILS (Function Codes 0x01, 0x05, 0x0F, Read/Write 1-bit, Base 00001+) */
/* ------------------------------------------------------------------------- */

/* Supervisory Commands (Auto-cleared or momentary triggers) */
#define MODBUS_COIL_CMD_ALARM_ACK_ALL        1U     /**< PDU 0x0000: Write 1 to acknowledge all active alarms */
#define MODBUS_COIL_CMD_BUZZER_MUTE          2U     /**< PDU 0x0001: Write 1 to mute acoustic alarm buzzer */
#define MODBUS_COIL_CMD_ALARM_RESET_LATCH    3U     /**< PDU 0x0002: Write 1 to reset latched alarm states */
#define MODBUS_COIL_CMD_FORCE_DUTY_ROTATION  4U     /**< PDU 0x0003: Write 1 to trigger immediate AC duty rotation */
#define MODBUS_COIL_CMD_TRIGGER_TELEMETRY_LOG 5U    /**< PDU 0x0004: Write 1 to force telemetry record flush to SD */
#define MODBUS_COIL_CMD_REBOOT_CONTROLLER    6U     /**< PDU 0x0005: Write 1 to request supervised system reboot */

/* Manual Unit Overrides (0 = Off / Auto, 1 = Force Run) */
#define MODBUS_COIL_CMD_AC1_FORCE_RUN        10U    /**< PDU 0x0009: AC1 Manual Force Run */
#define MODBUS_COIL_CMD_AC2_FORCE_RUN        11U    /**< PDU 0x000A: AC2 Manual Force Run */
#define MODBUS_COIL_CMD_AC3_FORCE_RUN        12U    /**< PDU 0x000B: AC3 Manual Force Run */
#define MODBUS_COIL_CMD_AC4_FORCE_RUN        13U    /**< PDU 0x000C: AC4 Manual Force Run */
#define MODBUS_COIL_CMD_AC5_FORCE_RUN        14U    /**< PDU 0x000D: AC5 Manual Force Run */
#define MODBUS_COIL_CMD_AC6_FORCE_RUN        15U    /**< PDU 0x000E: AC6 Manual Force Run */
#define MODBUS_COIL_CMD_AC7_FORCE_RUN        16U    /**< PDU 0x000F: AC7 Manual Force Run */
#define MODBUS_COIL_CMD_AC8_FORCE_RUN        17U    /**< PDU 0x0010: AC8 Manual Force Run */

/* ------------------------------------------------------------------------- */
/* 3. INPUT REGISTERS (Function Code 0x04, Read-Only 16-bit, Base 30001+)    */
/* ------------------------------------------------------------------------- */

/* Section 3.1: System & HVAC Group State (PDU Offsets 0x0000 .. 0x001F) */
#define MODBUS_IR_OFFSET_SYS_UPTIME_SEC_HI   0x0000U /**< System Uptime seconds (high 16-bit word) */
#define MODBUS_IR_OFFSET_SYS_UPTIME_SEC_LO   0x0001U /**< System Uptime seconds (low 16-bit word) */
#define MODBUS_IR_OFFSET_HVAC_GROUP_STATE    0x0002U /**< Group 1..4 operational composite state enum */
#define MODBUS_IR_OFFSET_ACTIVE_UNITS_MASK   0x0003U /**< Bitmask of currently running AC units (Bit 0..7) */
#define MODBUS_IR_OFFSET_FAULT_UNITS_MASK    0x0004U /**< Bitmask of faulted/locked-out AC units (Bit 0..7) */
#define MODBUS_IR_OFFSET_T_CTRL_G1_AVG       0x0005U /**< Group 1 Control Temp Avg (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_CTRL_G1_MAX       0x0006U /**< Group 1 Control Temp Max (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_CTRL_G1_MIN       0x0007U /**< Group 1 Control Temp Min (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_CTRL_G2_AVG       0x0008U /**< Group 2 Control Temp Avg (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_CTRL_G3_AVG       0x0009U /**< Group 3 Control Temp Avg (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_CTRL_G4_AVG       0x000AU /**< Group 4 Control Temp Avg (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_T_OUTDOOR_AVG       0x000BU /**< Outdoor Condenser Temp Avg (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_DUTY_ROTATION_REMAIN 0x000CU /**< Remaining hours until next scheduled rotation */
#define MODBUS_IR_OFFSET_SYSTEM_STATUS_FLAGS 0x000DU /**< Bitmask: bit 0: Degraded, bit 1: SD ready, bit 2: Net ready */

/* Section 3.2: Alarm Summary Bitmasks & Severity (PDU Offsets 0x0020 .. 0x003F) */
#define MODBUS_IR_OFFSET_ALARM_ACTIVE_MASK_0 0x0020U /**< Active Alarms Bitmask 0 (Alarms 1..16) */
#define MODBUS_IR_OFFSET_ALARM_ACTIVE_MASK_1 0x0021U /**< Active Alarms Bitmask 1 (Alarms 17..32) */
#define MODBUS_IR_OFFSET_ALARM_ACTIVE_MASK_2 0x0022U /**< Active Alarms Bitmask 2 (Alarms 33..48) */
#define MODBUS_IR_OFFSET_ALARM_ACTIVE_MASK_3 0x0023U /**< Active Alarms Bitmask 3 (Alarms 49..64) */
#define MODBUS_IR_OFFSET_ALARM_UNACK_MASK_0  0x0024U /**< Unacknowledged Alarms Bitmask 0 (Alarms 1..16) */
#define MODBUS_IR_OFFSET_ALARM_UNACK_MASK_1  0x0025U /**< Unacknowledged Alarms Bitmask 1 (Alarms 17..32) */
#define MODBUS_IR_OFFSET_ALARM_UNACK_MASK_2  0x0026U /**< Unacknowledged Alarms Bitmask 2 (Alarms 33..48) */
#define MODBUS_IR_OFFSET_ALARM_UNACK_MASK_3  0x0027U /**< Unacknowledged Alarms Bitmask 3 (Alarms 49..64) */
#define MODBUS_IR_OFFSET_ALARM_BUZZER_STATE  0x0028U /**< Buzzer State (0 = Silent, 1 = Beeping, 2 = Muted) */
#define MODBUS_IR_OFFSET_ALARM_MAX_SEVERITY  0x0029U /**< Highest active severity (0: Info, 1: Warn, 2: Crit, 3: Fatal) */
#define MODBUS_IR_OFFSET_ALARM_TOTAL_ACTIVE  0x002AU /**< Total count of active alarms */

/* Section 3.3: Energy Meter JSY-MK-339 (PDU Offsets 0x0040 .. 0x006F) */
#define MODBUS_IR_OFFSET_ENERGY_VA           0x0040U /**< Voltage Phase A (Scale: 0.1 V, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_VB           0x0041U /**< Voltage Phase B (Scale: 0.1 V, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_VC           0x0042U /**< Voltage Phase C (Scale: 0.1 V, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_IA           0x0043U /**< Current Phase A (Scale: 0.01 A, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_IB           0x0044U /**< Current Phase B (Scale: 0.01 A, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_IC           0x0045U /**< Current Phase C (Scale: 0.01 A, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_P_ACTIVE     0x0046U /**< Total Active Power (Scale: 0.1 kW, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_P_REACTIVE   0x0047U /**< Total Reactive Power (Scale: 0.1 kVar, int16) */
#define MODBUS_IR_OFFSET_ENERGY_POWER_FACTOR 0x0048U /**< Total Power Factor (Scale: 0.01, int16) */
#define MODBUS_IR_OFFSET_ENERGY_FREQUENCY    0x0049U /**< Grid AC Frequency (Scale: 0.1 Hz, uint16) */
#define MODBUS_IR_OFFSET_ENERGY_KWH_HI       0x004AU /**< Total Forward Active Energy (High 16-bit word, 0.1 kWh) */
#define MODBUS_IR_OFFSET_ENERGY_KWH_LO       0x004BU /**< Total Forward Active Energy (Low 16-bit word, 0.1 kWh) */

/* Section 3.4: RTD Temperature Bank CWT-TM-16PT (PDU Offsets 0x0070 .. 0x009F) */
#define MODBUS_IR_OFFSET_RTD_CH1             0x0070U /**< RTD Channel 1 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH2             0x0071U /**< RTD Channel 2 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH3             0x0072U /**< RTD Channel 3 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH4             0x0073U /**< RTD Channel 4 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH5             0x0074U /**< RTD Channel 5 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH6             0x0075U /**< RTD Channel 6 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH7             0x0076U /**< RTD Channel 7 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH8             0x0077U /**< RTD Channel 8 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH9             0x0078U /**< RTD Channel 9 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH10            0x0079U /**< RTD Channel 10 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH11            0x007AU /**< RTD Channel 11 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH12            0x007BU /**< RTD Channel 12 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH13            0x007CU /**< RTD Channel 13 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH14            0x007DU /**< RTD Channel 14 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH15            0x007EU /**< RTD Channel 15 Temp (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_RTD_CH16            0x007FU /**< RTD Channel 16 Temp (Scale: 0.1 °C, int16) */

/* Section 3.5: Revision F Thermodynamic Model (PDU Offsets 0x00A0 .. 0x00CF) */
#define MODBUS_IR_OFFSET_THERMAL_Q_SENS      0x00A0U /**< Sensible Cooling Capacity (Scale: 0.1 kW, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_Q_LAT       0x00A1U /**< Latent Cooling Capacity (Scale: 0.1 kW, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_Q_TOT       0x00A2U /**< Total Cooling Capacity (Scale: 0.1 kW, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_COP_REAL    0x00A3U /**< Realized Coefficient of Performance (Scale: 0.01, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_COP_SENS    0x00A4U /**< Sensible COP (Scale: 0.01, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_AIRFLOW     0x00A5U /**< Total Evaporator Air Flow (Scale: 1.0 m3/h, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_DEW_POINT   0x00A6U /**< Calculated Dew Point (Scale: 0.1 °C, int16) */
#define MODBUS_IR_OFFSET_THERMAL_RETURN_RH   0x00A7U /**< Return Air Relative Humidity (Scale: 0.1 %, uint16) */
#define MODBUS_IR_OFFSET_THERMAL_DELTAT_AVG  0x00A8U /**< Average Supply-Return Temp Differential (Scale: 0.1 °C, int16) */

/* ------------------------------------------------------------------------- */
/* 4. HOLDING REGISTERS (Function Codes 0x03, 0x06, 0x10, Read/Write 16-bit)  */
/* ------------------------------------------------------------------------- */

/* Section 4.1: HVAC Control Parameters (PDU Offsets 0x0000 .. 0x001F) */
#define MODBUS_HR_OFFSET_HVAC_MODE           0x0000U /**< Operation Mode (0: Telemetry, 1: Control, 2: Hybrid) */
#define MODBUS_HR_OFFSET_T_TARGET            0x0001U /**< Temperature Target / Setpoint (Scale: 0.1 °C, 15.0..30.0 °C) */
#define MODBUS_HR_OFFSET_T_HYST              0x0002U /**< Temperature Hysteresis (Scale: 0.1 °C, 0.5..5.0 °C) */
#define MODBUS_HR_OFFSET_T_ALARM_HI          0x0003U /**< High Temperature Alarm Threshold (Scale: 0.1 °C, 25.0..45.0 °C) */
#define MODBUS_HR_OFFSET_T_BYPASS            0x0004U /**< Critical Bypass Temperature (Scale: 0.1 °C, 28.0..50.0 °C) */
#define MODBUS_HR_OFFSET_T_TEMP_LO           0x0005U /**< Low Temperature Alarm Threshold (Scale: 0.1 °C, 5.0..18.0 °C) */
#define MODBUS_HR_OFFSET_ROTATION_HOURS      0x0006U /**< Scheduled Duty Rotation Interval (Hours, 1..720 h) */
#define MODBUS_HR_OFFSET_DATALOGGER_INTERVAL 0x0007U /**< MicroSD Datalogger Period (Minutes, 1..60 min) */
#define MODBUS_HR_OFFSET_MANUAL_OVERRIDE_MASK 0x0008U /**< Manual Override Enable Mask (Bit 0..7 for AC1..8) */

/* Section 4.2: Thermal Site Parameters (PDU Offsets 0x0020 .. 0x003F) */
#define MODBUS_HR_OFFSET_SITE_ALTITUDE       0x0020U /**< Facility Altitude above sea level (Meters, 0..5000 m) */
#define MODBUS_HR_OFFSET_SITE_FLOOR_AREA     0x0021U /**< Server Room Floor Surface Area (Scale: 0.1 m2, 10.0..5000.0 m2) */
#define MODBUS_HR_OFFSET_NOMINAL_AIRFLOW     0x0022U /**< Total Nominal Fan Design Airflow (Scale: 1.0 m3/h, uint16) */

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_TCP_TYPES_H_ */
