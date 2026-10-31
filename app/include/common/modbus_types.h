/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modbus RTU data contracts shared by every layer of the application.
 * Pure C99/C11 - no Zephyr headers, no vendor headers, no dynamic memory.
 */

#ifndef APP_COMMON_MODBUS_TYPES_H_
#define APP_COMMON_MODBUS_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * LIMITS AND TUNING CONSTANTS
 *============================================================================*/

/** Maximum number of slave devices accepted on a single physical bus. */
#define MODBUS_MAX_SLAVES_PER_BUS 16U

/** Maximum number of slave devices accepted across all buses. */
#define MODBUS_MAX_TOTAL_SLAVES 32U

/** Valid Modbus unit identifier range (broadcast address 0 excluded). */
#define MODBUS_SLAVE_ADDR_MIN 1U
#define MODBUS_SLAVE_ADDR_MAX 247U

/** Consecutive communication failures before a slave enters slow backoff. */
#define MODBUS_CONSECUTIVE_FAILS_THRESHOLD 3U

/** Poll interval applied to a slave in @ref MODBUS_HEALTH_OFFLINE_BACKOFF. */
#define MODBUS_OFFLINE_BACKOFF_INTERVAL_MS 10000U

/**
 * Age after which an unreleased claim is considered abandoned.
 *
 * A polling thread that dies between claiming and recording would otherwise
 * park a slave forever; the registry reclaims it after this interval.
 */
#define MODBUS_CLAIM_STALE_MS 5000U

/** Supported serial line bit rates for a Modbus RTU bus. */
#define MODBUS_BAUDRATE_MIN 1200U
#define MODBUS_BAUDRATE_MAX 921600U

/** Largest register block a single FC03 (read holding registers) may request. */
#define MODBUS_READ_HOLDING_MAX_REGISTERS 125U

/*============================================================================*
 * ENUMERATIONS
 *============================================================================*/

/**
 * @brief Logical identification of the physical RS-485 buses.
 *
 * The numeric value is used as an array index by the composition root, so
 * consecutive values starting at zero are required.
 */
typedef enum {
    MODBUS_BUS_RS485_1 = 0, /**< RS485-1 (USART2 on BrightC Controller V3) */
    MODBUS_BUS_RS485_2,     /**< RS485-2 (USART6 on BrightC Controller V3) */
    MODBUS_BUS_MAX          /**< Number of managed buses; not a valid bus id */
} modbus_bus_id_t;

/**
 * @brief Outcome of a single Modbus transaction.
 *
 * The distinction matters: an exception response proves the slave is alive and
 * must not push it into offline backoff, while a CRC error is a communication
 * failure even though bytes were received.
 */
typedef enum {
    MODBUS_RESULT_OK = 0,  /**< Request answered with valid data */
    MODBUS_RESULT_TIMEOUT, /**< No complete response inside the timeout window */
    MODBUS_RESULT_CRC,     /**< Response received but frame integrity failed */
    MODBUS_RESULT_EXCEPTION, /**< Slave answered with a Modbus exception code */
    MODBUS_RESULT_INVALID  /**< Request rejected locally, nothing was transmitted */
} modbus_transaction_result_t;

/**
 * @brief Runtime health of a polled slave.
 */
typedef enum {
    MODBUS_HEALTH_ONLINE = 0,    /**< Last transactions succeeded */
    MODBUS_HEALTH_DEGRADED,      /**< Between 1 and threshold-1 consecutive failures */
    MODBUS_HEALTH_OFFLINE_BACKOFF /**< Threshold reached; polled at the slow interval */
} modbus_slave_health_t;

/**
 * @brief Serial parity selection, expressed in protocol terms (0/1/2 style).
 */
typedef enum {
    MODBUS_PARITY_NONE = 0, /**< No parity (8N1, 8N2) */
    MODBUS_PARITY_EVEN,     /**< Even parity */
    MODBUS_PARITY_ODD       /**< Odd parity */
} modbus_parity_t;

/**
 * @brief Known field device types supported by the telemetry system.
 */
typedef enum {
    MODBUS_DEV_TYPE_UNKNOWN = 0,      /**< Not identified */
    MODBUS_DEV_TYPE_CWT_TM16PT,       /**< 16-channel PT100 temperature transmitter */
    MODBUS_DEV_TYPE_ENERGY_METER_SDM, /**< Industrial Eastron/Schneider energy meter */
    MODBUS_DEV_TYPE_GPIO_EXPANDER,    /**< Discrete inputs and relay outputs */
    MODBUS_DEV_TYPE_JSY_MK339         /**< JSY-MK-339 3-phase AC electrical energy meter */
} modbus_device_type_t;

#define MODBUS_DEV_TYPE_ENERGY_METER_JSY MODBUS_DEV_TYPE_JSY_MK339

/*============================================================================*
 * DATA STRUCTURES
 *============================================================================*/

/**
 * @brief Electrical configuration of one physical bus.
 *
 * Natural alignment on purpose: this structure is used in RAM by the polling
 * threads. Persistence uses an explicit, versioned serialization instead of a
 * raw memcpy of this layout.
 */
typedef struct {
    uint32_t baudrate;       /**< Bits per second (1200..921600) */
    modbus_parity_t parity;  /**< Line parity */
    uint8_t stop_bits;       /**< 1 or 2 */
    uint8_t data_bits;       /**< Always 8; kept explicit for validation */
} modbus_bus_cfg_t;

/**
 * @brief Configuration of one slave device.
 *
 * Flat, pointer-free and fixed size so it can be serialized for microconf or
 * EEPROM storage.
 */
typedef struct {
    uint8_t device_id;         /**< Application-unique logical id (never 0) */
    uint8_t bus_id;            /**< modbus_bus_id_t */
    uint8_t slave_addr;        /**< Modbus unit identifier (1..247) */
    uint8_t dev_type;          /**< modbus_device_type_t */
    uint16_t poll_interval_ms; /**< Normal polling period */
    uint16_t timeout_ms;       /**< Per-transaction response timeout */
    bool is_enabled;           /**< False when the device is administratively disabled */
} modbus_slave_descriptor_t;

/**
 * @brief Runtime statistics and health of one slave device.
 */
typedef struct {
    modbus_slave_health_t health;   /**< Current health classification */
    uint32_t successful_polls;      /**< Transactions completed with valid data */
    uint32_t timeout_count;         /**< Transactions with no response */
    uint32_t crc_error_count;       /**< Transactions with a corrupt frame */
    uint32_t exception_count;       /**< Modbus exception responses received */
    uint32_t consecutive_fails;     /**< Consecutive communication failures */
    uint64_t last_attempt_ms;       /**< Timestamp of the last claimed transaction */
    uint64_t last_poll_timestamp_ms;/**< Timestamp of the last completed transaction */
    uint64_t last_success_timestamp_ms; /**< Timestamp of the last successful transaction */
    bool has_polled;                /**< False until the first transaction completes */
    bool in_flight;                 /**< A thread has claimed this slave and not recorded yet */
} modbus_slave_runtime_state_t;

/**
 * @brief Everything one polling thread needs to reach a slave on the wire.
 */
typedef struct {
    uint8_t device_id;  /**< Logical id, echoed back by the caller */
    uint8_t slave_addr; /**< Modbus unit identifier to address */
    uint8_t dev_type;   /**< Device type, selects the polling strategy */
    uint16_t timeout_ms;/**< Response timeout to pass to the client */
} modbus_poll_target_t;

/**
 * @brief Immutable copy of one slave slot, safe to use without holding a lock.
 */
typedef struct {
    modbus_slave_descriptor_t descriptor; /**< Configuration snapshot */
    modbus_slave_runtime_state_t runtime; /**< Health and statistics snapshot */
} modbus_slave_snapshot_t;

/*============================================================================*
 * PURE VALIDATION HELPERS
 *
 * These predicates validate values that are part of the shared contract, so
 * they can be used by port wrappers (cheap structural checks) and by the
 * implementations (stateful checks) without duplicating the rules.
 *============================================================================*/

/**
 * @brief Check that a logical bus identifier is in range.
 *
 * @param[in] bus_id Candidate identifier, usually a modbus_bus_id_t.
 * @return true when the value names a managed bus.
 */
static inline bool modbus_bus_id_is_valid(uint8_t bus_id)
{
    return bus_id < (uint8_t) MODBUS_BUS_MAX;
}

/**
 * @brief Check that a Modbus unit identifier is addressable (broadcast excluded).
 *
 * @param[in] slave_addr Candidate identifier.
 * @return true for 1..247.
 */
static inline bool modbus_slave_addr_is_valid(uint8_t slave_addr)
{
    return slave_addr >= MODBUS_SLAVE_ADDR_MIN && slave_addr <= MODBUS_SLAVE_ADDR_MAX;
}

/**
 * @brief Check that a transaction outcome is one of the defined values.
 *
 * @param[in] result Candidate outcome.
 * @return true for every member of @ref modbus_transaction_result_t.
 */
static inline bool modbus_transaction_result_is_valid(modbus_transaction_result_t result)
{
    return (unsigned int) result <= (unsigned int) MODBUS_RESULT_INVALID;
}

/**
 * @brief Check that a bus configuration is within the supported ranges.
 *
 * @param[in] cfg Configuration to validate (may be NULL).
 * @return true when every field is usable.
 */
static inline bool modbus_bus_cfg_is_valid(const modbus_bus_cfg_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }

    if (cfg->baudrate < MODBUS_BAUDRATE_MIN || cfg->baudrate > MODBUS_BAUDRATE_MAX) {
        return false;
    }

    if (cfg->parity != MODBUS_PARITY_NONE && cfg->parity != MODBUS_PARITY_EVEN &&
        cfg->parity != MODBUS_PARITY_ODD) {
        return false;
    }

    if (cfg->stop_bits != 1U && cfg->stop_bits != 2U) {
        return false;
    }

    return cfg->data_bits == 8U;
}

/**
 * @brief Check that a slave descriptor is internally consistent.
 *
 * @param[in] desc Descriptor to validate (may be NULL).
 * @return true when the descriptor can be registered without modification.
 */
static inline bool modbus_slave_descriptor_is_valid(const modbus_slave_descriptor_t *desc)
{
    if (desc == NULL) {
        return false;
    }

    return desc->device_id != 0U && modbus_bus_id_is_valid(desc->bus_id) &&
           modbus_slave_addr_is_valid(desc->slave_addr) && desc->poll_interval_ms > 0U &&
           desc->timeout_ms > 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_TYPES_H_ */
