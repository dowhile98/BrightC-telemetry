/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port interface for zero-malloc JSON serialization & deserialization.
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_json_serializer_api_t)
 * 2) Handle (i_json_serializer_t)
 * 3) Validated wrappers (static inline i_json_serializer_*())
 *
 * Designed for compile-time descriptor-driven JSON codecs (e.g. Zephyr <zephyr/data/json.h>),
 * completely eliminating dynamic heap allocation (malloc/free) and heap fragmentation.
 *
 * Pure C99/C11 - zero Zephyr headers, zero vendor HAL, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_JSON_SERIALIZER_H_
#define APP_INTERFACES_I_JSON_SERIALIZER_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/web_server_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /**
     * @brief Encode HVAC status DTO to JSON string in fixed buffer.
     */
    int (*serialize_hvac_status)(void *impl, const rest_hvac_status_dto_t *dto,
                                char *out_buf, size_t buf_size, size_t *out_len);

    /**
     * @brief Parse JSON string into HVAC setpoint DTO.
     */
    int (*deserialize_hvac_setpoint)(void *impl, const char *json_str, size_t json_len,
                                    rest_hvac_setpoint_dto_t *out_dto);

    /**
     * @brief Encode Energy status DTO to JSON string in fixed buffer.
     */
    int (*serialize_energy_status)(void *impl, const rest_energy_status_dto_t *dto,
                                  char *out_buf, size_t buf_size, size_t *out_len);

    /**
     * @brief Encode Thermal status DTO to JSON string in fixed buffer.
     */
    int (*serialize_thermal_status)(void *impl, const rest_thermal_status_dto_t *dto,
                                   char *out_buf, size_t buf_size, size_t *out_len);

    /**
     * @brief Parse JSON string into Alarm Ack DTO.
     */
    int (*deserialize_alarm_ack)(void *impl, const char *json_str, size_t json_len,
                                rest_alarm_ack_dto_t *out_dto);
} i_json_serializer_api_t;

/*============================================================================*
 * 2. PORT HANDLE (injected into consumers / active objects)
 *============================================================================*/

typedef struct {
    const i_json_serializer_api_t *api; /**< V-table pointer */
    void *impl;                         /**< Concrete adapter context pointer */
} i_json_serializer_t;

/**
 * @brief Check that a JSON serializer port handle is bound.
 *
 * @param[in] ser Port handle to check (may be NULL).
 * @return true if both api and impl pointers are non-NULL and core operations are present.
 */
static inline bool i_json_serializer_is_bound(const i_json_serializer_t *ser)
{
    return (ser != NULL &&
            ser->api != NULL &&
            ser->impl != NULL &&
            ser->api->serialize_hvac_status != NULL &&
            ser->api->deserialize_hvac_setpoint != NULL &&
            ser->api->serialize_energy_status != NULL &&
            ser->api->serialize_thermal_status != NULL &&
            ser->api->deserialize_alarm_ack != NULL);
}

/*============================================================================*
 * 3. VALIDATED WRAPPERS (public API called by application & HTTP handlers)
 *============================================================================*/

/**
 * @brief Encode HVAC status DTO into JSON string.
 */
static inline int i_json_serializer_serialize_hvac_status(
    const i_json_serializer_t *ser,
    const rest_hvac_status_dto_t *dto,
    char *out_buf,
    size_t buf_size,
    size_t *out_len)
{
    if (!i_json_serializer_is_bound(ser) || dto == NULL || out_buf == NULL || buf_size == 0) {
        return -EINVAL;
    }
    return ser->api->serialize_hvac_status(ser->impl, dto, out_buf, buf_size, out_len);
}

/**
 * @brief Parse JSON string into HVAC setpoint DTO.
 */
static inline int i_json_serializer_deserialize_hvac_setpoint(
    const i_json_serializer_t *ser,
    const char *json_str,
    size_t json_len,
    rest_hvac_setpoint_dto_t *out_dto)
{
    if (!i_json_serializer_is_bound(ser) || json_str == NULL || json_len == 0 || out_dto == NULL) {
        return -EINVAL;
    }
    return ser->api->deserialize_hvac_setpoint(ser->impl, json_str, json_len, out_dto);
}

/**
 * @brief Encode Energy status DTO into JSON string.
 */
static inline int i_json_serializer_serialize_energy_status(
    const i_json_serializer_t *ser,
    const rest_energy_status_dto_t *dto,
    char *out_buf,
    size_t buf_size,
    size_t *out_len)
{
    if (!i_json_serializer_is_bound(ser) || dto == NULL || out_buf == NULL || buf_size == 0) {
        return -EINVAL;
    }
    return ser->api->serialize_energy_status(ser->impl, dto, out_buf, buf_size, out_len);
}

/**
 * @brief Encode Thermal status DTO into JSON string.
 */
static inline int i_json_serializer_serialize_thermal_status(
    const i_json_serializer_t *ser,
    const rest_thermal_status_dto_t *dto,
    char *out_buf,
    size_t buf_size,
    size_t *out_len)
{
    if (!i_json_serializer_is_bound(ser) || dto == NULL || out_buf == NULL || buf_size == 0) {
        return -EINVAL;
    }
    return ser->api->serialize_thermal_status(ser->impl, dto, out_buf, buf_size, out_len);
}

/**
 * @brief Parse JSON string into Alarm Ack DTO.
 */
static inline int i_json_serializer_deserialize_alarm_ack(
    const i_json_serializer_t *ser,
    const char *json_str,
    size_t json_len,
    rest_alarm_ack_dto_t *out_dto)
{
    if (!i_json_serializer_is_bound(ser) || json_str == NULL || json_len == 0 || out_dto == NULL) {
        return -EINVAL;
    }
    return ser->api->deserialize_alarm_ack(ser->impl, json_str, json_len, out_dto);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_JSON_SERIALIZER_H_ */
