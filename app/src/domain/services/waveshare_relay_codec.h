/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure register/coil codec for the Waveshare Modbus RTU Relay 32CH.
 * Pure C99/C11 - no Zephyr headers, no transport, no hardware dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_WAVESHARE_RELAY_CODEC_H_
#define APP_DOMAIN_SERVICES_WAVESHARE_RELAY_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of relay channels on the Waveshare 32CH module. */
#define WAVESHARE_RELAY_32CH_CHANNEL_COUNT 32U

/** Coil address range for individual relays (0x0000 - 0x001F). */
#define WAVESHARE_RELAY_COIL_ADDR_BASE 0x0000U
#define WAVESHARE_RELAY_COIL_ADDR_MAX  0x001FU

/** FC05 single-coil command values. */
#define WAVESHARE_RELAY_COIL_ON   0xFF00U /**< Turn relay on */
#define WAVESHARE_RELAY_COIL_OFF  0x0000U /**< Turn relay off */
#define WAVESHARE_RELAY_COIL_TOGGLE 0x5500U /**< Toggle relay state */

/** FC05 address for controlling all 32 relays at once. */
#define WAVESHARE_RELAY_COIL_ADDR_ALL 0x00FFU

/** Bytes needed to represent 32 coils bit-packed LSB-first. */
#define WAVESHARE_RELAY_COIL_BYTES 4U

/**
 * @brief Parse a FC01 read-coils response for all 32 relays.
 *
 * The Waveshare returns the relay states as a little-endian bitmask:
 * byte 0 contains coils 0..7 (bit 0 = coil 0), byte 1 contains coils 8..15,
 * etc. Idle high bits are zero.
 *
 * @param[in]  data      Response payload (at least @ref WAVESHARE_RELAY_COIL_BYTES bytes).
 * @param[in]  data_len  Number of bytes available in @p data.
 * @param[out] states    Destination array of @ref WAVESHARE_RELAY_32CH_CHANNEL_COUNT
 *                       @ref relay_state_t values.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int waveshare_relay_parse_coils_response(const uint8_t *data, size_t data_len,
                                         relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT]);

/**
 * @brief Build the FC05 single-coil command value from a relay state.
 *
 * @param[in]  state  Desired relay state.
 * @param[out] out    Packed 16-bit command value (0xFF00 / 0x0000 / 0x5500).
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int waveshare_relay_build_single_coil_value(relay_state_t state, uint16_t *out);

/**
 * @brief Pack relay states into a FC0F bit-packed payload (LSB-first).
 *
 * Writes @p channel_count bytes (one byte per 8 channels), with bit 0 of byte 0
 * representing channel 0, bit 1 channel 1, etc. Unused high bits are zero.
 *
 * @param[in]  states       Array of @p channel_count relay states.
 * @param[in]  channel_count Number of channels to pack (1..32).
 * @param[out] out          Destination buffer of at least ceil(channel_count / 8) bytes.
 * @param[out] out_bytes    Number of bytes written to @p out.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int waveshare_relay_pack_coils(const relay_state_t *states, size_t channel_count,
                               uint8_t *out, size_t *out_bytes);

/**
 * @brief Unpack a FC01/FE0F bit-packed payload into relay states.
 *
 * @param[in]  data         Bit-packed payload, LSB-first.
 * @param[in]  data_len     Number of bytes in @p data.
 * @param[out] states       Destination array of at least @p channel_count entries.
 * @param[in]  channel_count Number of channels to unpack (1..32).
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int waveshare_relay_unpack_coils(const uint8_t *data, size_t data_len,
                                 relay_state_t *states, size_t channel_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_WAVESHARE_RELAY_CODEC_H_ */
