/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the Waveshare Modbus RTU Relay 32CH coil codec.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "domain/services/waveshare_relay_codec.h"

ZTEST_SUITE(waveshare_relay_codec_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_parse_coils_all_off)
{
    uint8_t data[WAVESHARE_RELAY_COIL_BYTES] = {0x00, 0x00, 0x00, 0x00};
    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];

    int err = waveshare_relay_parse_coils_response(data, sizeof(data), states);
    zassert_equal(err, 0, "Parser returned error");

    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        zassert_equal(states[i], RELAY_STATE_OPEN, "Ch%u must be open", (unsigned int) i);
    }
}

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_parse_coils_all_on)
{
    uint8_t data[WAVESHARE_RELAY_COIL_BYTES] = {0xFF, 0xFF, 0xFF, 0xFF};
    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];

    int err = waveshare_relay_parse_coils_response(data, sizeof(data), states);
    zassert_equal(err, 0, "Parser returned error");

    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        zassert_equal(states[i], RELAY_STATE_CLOSED, "Ch%u must be closed", (unsigned int) i);
    }
}

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_parse_coils_alternating)
{
    /* Byte 0 = 0x01 -> coil 0 on, rest off */
    /* Byte 1 = 0x01 -> coil 8 on, rest off */
    /* Byte 2 = 0x01 -> coil 16 on, rest off */
    /* Byte 3 = 0x01 -> coil 24 on, rest off */
    uint8_t data[WAVESHARE_RELAY_COIL_BYTES] = {0x01, 0x01, 0x01, 0x01};
    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];

    int err = waveshare_relay_parse_coils_response(data, sizeof(data), states);
    zassert_equal(err, 0, "Parser returned error");

    zassert_equal(states[0], RELAY_STATE_CLOSED, "Ch0 must be closed");
    zassert_equal(states[1], RELAY_STATE_OPEN, "Ch1 must be open");
    zassert_equal(states[8], RELAY_STATE_CLOSED, "Ch8 must be closed");
    zassert_equal(states[16], RELAY_STATE_CLOSED, "Ch16 must be closed");
    zassert_equal(states[24], RELAY_STATE_CLOSED, "Ch24 must be closed");
    zassert_equal(states[31], RELAY_STATE_OPEN, "Ch31 must be open");
}

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_build_single_coil_value)
{
    uint16_t value = 0;

    int err = waveshare_relay_build_single_coil_value(RELAY_STATE_CLOSED, &value);
    zassert_equal(err, 0, "Build closed failed");
    zassert_equal(value, WAVESHARE_RELAY_COIL_ON, "Expected 0xFF00 for closed");

    value = 0;
    err = waveshare_relay_build_single_coil_value(RELAY_STATE_OPEN, &value);
    zassert_equal(err, 0, "Build open failed");
    zassert_equal(value, WAVESHARE_RELAY_COIL_OFF, "Expected 0x0000 for open");

    err = waveshare_relay_build_single_coil_value(RELAY_STATE_CLOSED, NULL);
    zassert_equal(err, -EINVAL, "NULL output must be rejected");
}

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_pack_coils)
{
    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];
    uint8_t out[WAVESHARE_RELAY_COIL_BYTES];
    size_t out_bytes = 0;

    /* All open -> all zero */
    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        states[i] = RELAY_STATE_OPEN;
    }

    int err = waveshare_relay_pack_coils(states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT, out,
                                         &out_bytes);
    zassert_equal(err, 0, "Pack failed");
    zassert_equal(out_bytes, WAVESHARE_RELAY_COIL_BYTES, "Byte count mismatch");
    zassert_equal(out[0], 0x00, "Byte0 must be 0");
    zassert_equal(out[1], 0x00, "Byte1 must be 0");
    zassert_equal(out[2], 0x00, "Byte2 must be 0");
    zassert_equal(out[3], 0x00, "Byte3 must be 0");

    /* All closed -> all 0xFF */
    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        states[i] = RELAY_STATE_CLOSED;
    }

    err = waveshare_relay_pack_coils(states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT, out,
                                     &out_bytes);
    zassert_equal(err, 0, "Pack all closed failed");
    zassert_equal(out[0], 0xFF, "Byte0 must be 0xFF");
    zassert_equal(out[1], 0xFF, "Byte1 must be 0xFF");
    zassert_equal(out[2], 0xFF, "Byte2 must be 0xFF");
    zassert_equal(out[3], 0xFF, "Byte3 must be 0xFF");

    /* Only channel 5 closed */
    for (size_t i = 0U; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        states[i] = RELAY_STATE_OPEN;
    }
    states[5] = RELAY_STATE_CLOSED;

    err = waveshare_relay_pack_coils(states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT, out,
                                     &out_bytes);
    zassert_equal(err, 0, "Pack single failed");
    zassert_equal(out[0], 0x20, "Byte0 must have bit5 set (0x20)");
    zassert_equal(out[1], 0x00, "Byte1 must be 0");
    zassert_equal(out[2], 0x00, "Byte2 must be 0");
    zassert_equal(out[3], 0x00, "Byte3 must be 0");

    err = waveshare_relay_pack_coils(NULL, WAVESHARE_RELAY_32CH_CHANNEL_COUNT, out, &out_bytes);
    zassert_equal(err, -EINVAL, "NULL states must be rejected");

    err = waveshare_relay_pack_coils(states, 0U, out, &out_bytes);
    zassert_equal(err, -EINVAL, "Zero channel count must be rejected");

    err = waveshare_relay_pack_coils(states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT + 1U, out,
                                     &out_bytes);
    zassert_equal(err, -EINVAL, "Too many channels must be rejected");
}

ZTEST(waveshare_relay_codec_suite, test_waveshare_relay_unpack_coils)
{
    relay_state_t states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];
    uint8_t data[WAVESHARE_RELAY_COIL_BYTES] = {0x01, 0x00, 0x00, 0x00};

    int err = waveshare_relay_unpack_coils(data, sizeof(data), states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
    zassert_equal(err, 0, "Unpack failed");
    zassert_equal(states[0], RELAY_STATE_CLOSED, "Ch0 must be closed");
    zassert_equal(states[1], RELAY_STATE_OPEN, "Ch1 must be open");
    zassert_equal(states[31], RELAY_STATE_OPEN, "Ch31 must be open");

    /* Short payload must be rejected */
    err = waveshare_relay_unpack_coils(data, 2U, states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
    zassert_equal(err, -EINVAL, "Short payload must be rejected");

    /* NULL args rejected */
    err = waveshare_relay_unpack_coils(NULL, sizeof(data), states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
    zassert_equal(err, -EINVAL, "NULL data must be rejected");

    err = waveshare_relay_unpack_coils(data, sizeof(data), NULL, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
    zassert_equal(err, -EINVAL, "NULL states must be rejected");

    err = waveshare_relay_unpack_coils(data, sizeof(data), states, 0U);
    zassert_equal(err, -EINVAL, "Zero channel count must be rejected");
}
