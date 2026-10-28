/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the CWT-TM-16PT temperature transmitter register decoder.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "domain/services/cwt_tm16pt_codec.h"

ZTEST_SUITE(cwt_tm16pt_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parse_scaled_normal)
{
    uint16_t simulated_regs[16];
    /* Channel 0: 254 = 25.4 C
     * Channel 1: 300 = 30.0 C
     * Channel 2: -150 = -15.0 C (int16 0xFF6A)
     * Channels 3-15: 200 = 20.0 C */
    simulated_regs[0] = 254;
    simulated_regs[1] = 300;
    simulated_regs[2] = (uint16_t)(-150);
    for (int i = 3; i < 16; i++) {
        simulated_regs[i] = 200;
    }

    temperature_bank_16ch_t bank;
    memset(&bank, 0, sizeof(bank));

    int err = cwt_tm16pt_parse_scaled_temperatures(simulated_regs, 16, &bank);
    zassert_equal(err, 0, "Parser returned error");
    zassert_true(bank.is_communication_valid, "Communication should be valid");
    zassert_equal(bank.open_probe_mask, 0, "No open probes expected");
    zassert_equal(bank.valid_channels_mask, 0xFFFF, "All 16 channels must be valid");

    zassert_within(bank.temperatures_c[0], 25.4f, 0.01f, "Ch0 mismatch");
    zassert_within(bank.temperatures_c[1], 30.0f, 0.01f, "Ch1 mismatch");
    zassert_within(bank.temperatures_c[2], -15.0f, 0.01f, "Ch2 negative temp mismatch");
    zassert_within(bank.temperatures_c[15], 20.0f, 0.01f, "Ch15 mismatch");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parse_scaled_open_probe_detection)
{
    uint16_t simulated_regs[16];
    for (int i = 0; i < 16; i++) {
        simulated_regs[i] = 220; /* 22.0 C */
    }

    /* Channel 3: Disconnected probe (0x7FFF = 32767) */
    simulated_regs[3] = CWT_TM16PT_PROBE_OPEN_CODE;
    /* Channel 7: Broken probe sentinel (-32768) */
    simulated_regs[7] = 0x8000;

    temperature_bank_16ch_t bank;
    memset(&bank, 0, sizeof(bank));

    int err = cwt_tm16pt_parse_scaled_temperatures(simulated_regs, 16, &bank);
    zassert_equal(err, 0, "Parser returned error");

    /* Channel 3 and Channel 7 must be in open probe mask */
    uint16_t expected_open = (1U << 3) | (1U << 7);
    zassert_equal(bank.open_probe_mask, expected_open, "Open probe mask mismatch");

    /* Valid mask must NOT have bit 3 and 7 */
    zassert_false((bank.valid_channels_mask & (1U << 3)), "Ch3 must not be valid");
    zassert_false((bank.valid_channels_mask & (1U << 7)), "Ch7 must not be valid");
    zassert_true((bank.valid_channels_mask & (1U << 0)), "Ch0 must be valid");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_config_register_build_and_parse)
{
    uint16_t reg_val = 0;

    /* Build config: Slave ID 1, 9600 bps, None Parity (0) */
    int err = cwt_tm16pt_build_config_register(1, 9600, 0, &reg_val);
    zassert_equal(err, 0, "Build failed for default 9600 8N1");
    zassert_equal(reg_val, 0x0100, "Expected 0x0100 for ID 1, 9600, None");

    uint8_t out_id = 0, out_parity = 0;
    uint32_t out_baud = 0;
    err = cwt_tm16pt_parse_config_register(reg_val, &out_id, &out_baud, &out_parity);
    zassert_equal(err, 0, "Parse failed");
    zassert_equal(out_id, 1, "Slave ID mismatch");
    zassert_equal(out_baud, 9600, "Baudrate mismatch");
    zassert_equal(out_parity, 0, "Parity mismatch");

    /* Build config: Slave ID 10, 19200 bps (code 6), Even Parity (code 1) */
    /* low byte: 6 | (1 << 3) = 6 | 8 = 14 = 0x0E. High byte: 10 = 0x0A -> 0x0A0E */
    err = cwt_tm16pt_build_config_register(10, 19200, 1, &reg_val);
    zassert_equal(err, 0, "Build failed for 19200 Even");
    zassert_equal(reg_val, 0x0A0E, "Expected 0x0A0E for ID 10, 19200, Even");

    err = cwt_tm16pt_parse_config_register(reg_val, &out_id, &out_baud, &out_parity);
    zassert_equal(err, 0, "Parse failed");
    zassert_equal(out_id, 10, "Slave ID mismatch");
    zassert_equal(out_baud, 19200, "Baudrate mismatch");
    zassert_equal(out_parity, 1, "Parity mismatch");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parse_float_normal_and_out_of_range)
{
    uint16_t simulated_regs[CWT_TM16PT_FLOAT_REG_COUNT];
    memset(simulated_regs, 0, sizeof(simulated_regs));

    /* Channel 0: 25.5 C = 0x41CC 0000 (big endian word order). */
    simulated_regs[0] = 0x41CCU;
    simulated_regs[1] = 0x0000U;

    /* Channel 1: -10.25 C = 0xC124 0000. */
    simulated_regs[2] = 0xC124U;
    simulated_regs[3] = 0x0000U;

    /* Channel 2: 0.0 C, a valid reading, not an open probe. */
    simulated_regs[4] = 0x0000U;
    simulated_regs[5] = 0x0000U;

    /* Channel 3: 1000.0 C = 0x447A 0000, outside the transmitter range. */
    simulated_regs[6] = 0x447AU;
    simulated_regs[7] = 0x0000U;

    temperature_bank_16ch_t bank;
    memset(&bank, 0, sizeof(bank));

    int err = cwt_tm16pt_parse_float_temperatures(simulated_regs, CWT_TM16PT_FLOAT_REG_COUNT,
                                                 &bank);
    zassert_equal(err, 0, "Float parser returned error");
    zassert_true(bank.is_communication_valid, "Communication should be valid");

    zassert_within(bank.temperatures_c[0], 25.5f, 0.001f, "Ch0 mismatched");
    zassert_within(bank.temperatures_c[1], -10.25f, 0.001f, "Ch1 negative mismatched");
    zassert_within(bank.temperatures_c[2], 0.0f, 0.001f, "Ch2 zero mismatched");

    zassert_true((bank.valid_channels_mask & (1U << 0)), "Ch0 must be valid");
    zassert_true((bank.valid_channels_mask & (1U << 1)), "Ch1 must be valid");
    zassert_true((bank.valid_channels_mask & (1U << 2)), "Ch2 must be valid");

    /* Out of range in float mode is reported as an open probe, not as a value. */
    zassert_true((bank.open_probe_mask & (1U << 3)), "Ch3 must be flagged");
    zassert_false((bank.valid_channels_mask & (1U << 3)), "Ch3 must not be valid");
}

ZTEST(cwt_tm16pt_suite, test_cwt_tm16pt_parsers_reject_bad_arguments)
{
    uint16_t regs[CWT_TM16PT_FLOAT_REG_COUNT] = {0};
    temperature_bank_16ch_t bank;
    uint16_t config_reg = 0U;

    memset(&bank, 0, sizeof(bank));

    zassert_equal(cwt_tm16pt_parse_scaled_temperatures(NULL, CWT_TM16PT_SCALED_REG_COUNT, &bank),
                  -EINVAL, "NULL registers must be rejected");
    zassert_equal(cwt_tm16pt_parse_scaled_temperatures(regs, CWT_TM16PT_SCALED_REG_COUNT, NULL),
                  -EINVAL, "NULL destination must be rejected");
    zassert_equal(cwt_tm16pt_parse_scaled_temperatures(regs, CWT_TM16PT_SCALED_REG_COUNT - 1U,
                                                      &bank),
                  -EINVAL, "A short register block must be rejected");

    zassert_equal(cwt_tm16pt_parse_float_temperatures(NULL, CWT_TM16PT_FLOAT_REG_COUNT, &bank),
                  -EINVAL, "NULL registers must be rejected");
    zassert_equal(cwt_tm16pt_parse_float_temperatures(regs, CWT_TM16PT_FLOAT_REG_COUNT - 1U, &bank),
                  -EINVAL, "A short float block must be rejected");

    zassert_equal(cwt_tm16pt_build_config_register(0U, 9600U, 0U, &config_reg), -EINVAL,
                  "Slave address 0 must be rejected");
    zassert_equal(cwt_tm16pt_build_config_register(1U, 9600U, 0U, NULL), -EINVAL,
                  "NULL register output must be rejected");
    zassert_equal(cwt_tm16pt_build_config_register(1U, 600U, 0U, &config_reg), -EINVAL,
                  "An unsupported baud rate must be rejected");
    zassert_equal(cwt_tm16pt_build_config_register(1U, 9600U, 9U, &config_reg), -EINVAL,
                  "An unsupported parity must be rejected");

    zassert_equal(cwt_tm16pt_parse_config_register(0x0100U, NULL, NULL, NULL), -EINVAL,
                  "NULL outputs must be rejected");
}
