/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file indicator_types.h
 * @brief Pure domain types for Status LED and Buzzer indicators.
 *
 * ISO C99 only. Zero Zephyr or vendor hardware dependencies.
 */

#ifndef APP_COMMON_INDICATOR_TYPES_H_
#define APP_COMMON_INDICATOR_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Visual signaling states for the status LED.
 */
typedef enum {
    LED_STATE_OFF = 0,          /**< LED steady off */
    LED_STATE_ON,               /**< LED steady on */
    LED_STATE_BLINK_SLOW,       /**< Slow blinking (~1 Hz, 50% duty) */
    LED_STATE_BLINK_FAST,       /**< Fast blinking (~5 Hz, 50% duty) */
    LED_STATE_BLINK_HEARTBEAT,  /**< Double-pulse heartbeat indicating system alive */
    LED_STATE_MAX               /**< Sentinel value */
} led_state_t;

/**
 * @brief Acoustic signaling patterns for the buzzer.
 */
typedef enum {
    BUZZER_PATTERN_SILENT = 0,          /**< Buzzer off / silent */
    BUZZER_PATTERN_BEEP_SHORT,          /**< Single short confirmation beep (~100 ms) */
    BUZZER_PATTERN_BEEP_LONG,           /**< Single long beep (~500 ms) */
    BUZZER_PATTERN_ALARM_INTERMITTENT,  /**< Repetitive tone bursts with pauses */
    BUZZER_PATTERN_ALARM_CONTINUOUS,    /**< Continuous tone */
    BUZZER_PATTERN_STARTUP_CHIME,       /**< Smart industrial rising two-tone chime */
    BUZZER_PATTERN_MAX                  /**< Sentinel value */
} buzzer_pattern_t;

/**
 * @brief System alarm severity levels.
 */
typedef enum {
    ALARM_LEVEL_NONE = 0,       /**< Normal operation / no active alarm */
    ALARM_LEVEL_INFO,           /**< Informative notification */
    ALARM_LEVEL_WARNING,        /**< Warning: non-fatal anomaly */
    ALARM_LEVEL_CRITICAL,       /**< Critical: safety or service fault */
    ALARM_LEVEL_MAX             /**< Sentinel value */
} alarm_level_t;

/**
 * @brief Global system operational health state.
 */
typedef enum {
    SYSTEM_HEALTH_HEALTHY = 0,  /**< All subsystems operating normally */
    SYSTEM_HEALTH_DEGRADED,     /**< Partial service degradation */
    SYSTEM_HEALTH_FAULT,        /**< Major fault condition */
    SYSTEM_HEALTH_MAX           /**< Sentinel value */
} system_health_t;

/**
 * @brief Manual indicator override commands (from Shell, CLI, or test harness).
 */
typedef enum {
    INDICATOR_CMD_AUTO = 0,     /**< Revert to automatic alarm/health control */
    INDICATOR_CMD_FORCE_LED,    /**< Force specific LED state */
    INDICATOR_CMD_FORCE_BUZZER, /**< Force specific buzzer pattern */
    INDICATOR_CMD_TEST,         /**< Run self-test indicator sequence */
    INDICATOR_CMD_MAX           /**< Sentinel value */
} indicator_cmd_t;

/**
 * @brief Manual indicator command payload.
 */
typedef struct {
    indicator_cmd_t  cmd;             /**< Command action */
    led_state_t      led_state;       /**< Applied if cmd == INDICATOR_CMD_FORCE_LED */
    buzzer_pattern_t buzzer_pattern;  /**< Applied if cmd == INDICATOR_CMD_FORCE_BUZZER */
    uint32_t         duration_ms;     /**< Duration in ms (0 = permanent until next command) */
} indicator_cmd_msg_t;

/**
 * @brief Current status of visual and acoustic indicators.
 */
typedef struct {
    led_state_t      led_state;          /**< Currently active LED visual state */
    buzzer_pattern_t buzzer_pattern;     /**< Currently active acoustic pattern */
    alarm_level_t    active_alarm_level; /**< Highest active alarm level */
    system_health_t  system_health;      /**< Current system health */
    bool             manual_override;    /**< True if manual override is active */
} indicator_status_t;

/**
 * @brief Alarm event message published across subsystems.
 */
typedef struct {
    uint16_t      alarm_id;     /**< Unique identifier for the alarm source */
    alarm_level_t level;        /**< Severity level */
    bool          is_active;    /**< True if active, false if cleared */
    uint32_t      timestamp_ms; /**< Timestamp of the event in ms */
} alarm_msg_t;

/**
 * @brief System health event message.
 */
typedef struct {
    system_health_t health;       /**< Global system health state */
    uint32_t        timestamp_ms; /**< Timestamp of the report in ms */
} system_health_msg_t;

/*============================================================================*
 * VALIDATION PREDICATES
 *============================================================================*/

/**
 * @brief Check if a LED state is valid.
 */
static inline bool led_state_is_valid(led_state_t state)
{
    return (unsigned int)state < (unsigned int)LED_STATE_MAX;
}

/**
 * @brief Check if a buzzer pattern is valid.
 */
static inline bool buzzer_pattern_is_valid(buzzer_pattern_t pattern)
{
    return (unsigned int)pattern < (unsigned int)BUZZER_PATTERN_MAX;
}

/**
 * @brief Check if an alarm level is valid.
 */
static inline bool alarm_level_is_valid(alarm_level_t level)
{
    return (unsigned int)level < (unsigned int)ALARM_LEVEL_MAX;
}

/**
 * @brief Check if a system health state is valid.
 */
static inline bool system_health_is_valid(system_health_t health)
{
    return (unsigned int)health < (unsigned int)SYSTEM_HEALTH_MAX;
}

/**
 * @brief Check if an indicator command is valid.
 */
static inline bool indicator_cmd_is_valid(indicator_cmd_t cmd)
{
    return (unsigned int)cmd < (unsigned int)INDICATOR_CMD_MAX;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_INDICATOR_TYPES_H_ */
