/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "waveshare_relay_codec.h"

#include <errno.h>
#include <string.h>

int waveshare_relay_parse_coils_response(const uint8_t *data, size_t data_len,
                                         relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT])
{
    if (data == NULL || states == NULL || data_len < WAVESHARE_RELAY_COIL_BYTES) {
        return -EINVAL;
    }

    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        size_t byte_index = i / 8U;
        uint8_t bit_mask = (uint8_t) (1U << (i % 8U));
        bool is_on = (data[byte_index] & bit_mask) != 0U;

        states[i] = is_on ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    }

    return 0;
}

int waveshare_relay_build_single_coil_value(relay_state_t state, uint16_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }

    switch (state) {
    case RELAY_STATE_CLOSED:
        *out = WAVESHARE_RELAY_COIL_ON;
        break;
    case RELAY_STATE_OPEN:
        *out = WAVESHARE_RELAY_COIL_OFF;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

int waveshare_relay_pack_coils(const relay_state_t *states, size_t channel_count,
                               uint8_t *out, size_t *out_bytes)
{
    if (states == NULL || out == NULL || out_bytes == NULL ||
        channel_count == 0U || channel_count > WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    size_t needed = (channel_count + 7U) / 8U;

    (void) memset(out, 0, needed);

    for (size_t i = 0U; i < channel_count; i++) {
        size_t byte_index = i / 8U;
        uint8_t bit_mask = (uint8_t) (1U << (i % 8U));

        if (states[i] == RELAY_STATE_CLOSED) {
            out[byte_index] = (uint8_t) (out[byte_index] | bit_mask);
        }
    }

    *out_bytes = needed;

    return 0;
}

int waveshare_relay_unpack_coils(const uint8_t *data, size_t data_len,
                                 relay_state_t *states, size_t channel_count)
{
    if (data == NULL || states == NULL || channel_count == 0U ||
        channel_count > WAVESHARE_RELAY_32CH_CHANNEL_COUNT) {
        return -EINVAL;
    }

    size_t needed = (channel_count + 7U) / 8U;

    if (data_len < needed) {
        return -EINVAL;
    }

    for (size_t i = 0U; i < channel_count; i++) {
        size_t byte_index = i / 8U;
        uint8_t bit_mask = (uint8_t) (1U << (i % 8U));
        bool is_on = (data[byte_index] & bit_mask) != 0U;

        states[i] = is_on ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    }

    return 0;
}
