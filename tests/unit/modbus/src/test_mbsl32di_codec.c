/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the MBSL32DI 32-channel digital input module codec.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "domain/services/mbsl32di_codec.h"

ZTEST_SUITE(mbsl32di_codec_suite, NULL, NULL, NULL, NULL, NULL);

/*============================================================================*
 * INPUT REGISTER PARSING TESTS
 *============================================================================*/

ZTEST(mbsl32di_codec_suite, test_parse_inputs_rejects_bad_arguments)
{
    uint16_t regs[2] = {0x0000, 0x0000};
    mbsl32di_reading_t reading;

    zassert_equal(mbsl32di_parse_inputs(NULL, 2U, &reading), -EINVAL, "NULL regs must fail");
    zassert_equal(mbsl32di_parse_inputs(regs, 2U, NULL), -EINVAL, "NULL reading must fail");
    zassert_equal(mbsl32di_parse_inputs(regs, 0U, &reading), -EINVAL, "0 count must fail");
    zassert_equal(mbsl32di_parse_inputs(regs, 1U, &reading), -EINVAL, "Short buffer (1 reg) must fail");
}

ZTEST(mbsl32di_codec_suite, test_parse_inputs_all_channels_off)
{
    uint16_t regs[2] = {0x0000, 0x0000};
    mbsl32di_reading_t reading;
    memset(&reading, 0xFF, sizeof(reading));

    int res = mbsl32di_parse_inputs(regs, 2U, &reading);
    zassert_equal(res, 0, "Parse inputs should succeed");
    zassert_true(reading.is_valid, "Reading must be valid");
    zassert_equal(reading.raw_inputs, 0x00000000U, "All inputs must be 0");

    for (uint8_t ch = 1U; ch <= 32U; ch++) {
        zassert_false(mbsl32di_get_channel(&reading, ch), "Channel %u should be false", ch);
    }
}

ZTEST(mbsl32di_codec_suite, test_parse_inputs_all_channels_on)
{
    uint16_t regs[2] = {0xFFFF, 0xFFFF};
    mbsl32di_reading_t reading;

    int res = mbsl32di_parse_inputs(regs, 2U, &reading);
    zassert_equal(res, 0, "Parse inputs should succeed");
    zassert_true(reading.is_valid, "Reading must be valid");
    zassert_equal(reading.raw_inputs, 0xFFFFFFFFU, "All inputs must be 1");

    for (uint8_t ch = 1U; ch <= 32U; ch++) {
        zassert_true(mbsl32di_get_channel(&reading, ch), "Channel %u should be true", ch);
    }
}

ZTEST(mbsl32di_codec_suite, test_parse_inputs_boundary_channels)
{
    mbsl32di_reading_t reading;

    /* Channel 1 (Bit 0 of Reg 0) */
    uint16_t regs_ch1[2] = {0x0001, 0x0000};
    zassert_equal(mbsl32di_parse_inputs(regs_ch1, 2U, &reading), 0, "Parse failed");
    zassert_equal(reading.raw_inputs, 0x00000001U, "Channel 1 bit 0 mismatch");
    zassert_true(mbsl32di_get_channel(&reading, 1U), "Channel 1 should be active");
    zassert_false(mbsl32di_get_channel(&reading, 2U), "Channel 2 should be inactive");

    /* Channel 16 (Bit 15 of Reg 0) */
    uint16_t regs_ch16[2] = {0x8000, 0x0000};
    zassert_equal(mbsl32di_parse_inputs(regs_ch16, 2U, &reading), 0, "Parse failed");
    zassert_equal(reading.raw_inputs, 0x00008000U, "Channel 16 bit 15 mismatch");
    zassert_true(mbsl32di_get_channel(&reading, 16U), "Channel 16 should be active");
    zassert_false(mbsl32di_get_channel(&reading, 15U), "Channel 15 should be inactive");
    zassert_false(mbsl32di_get_channel(&reading, 17U), "Channel 17 should be inactive");

    /* Channel 17 (Bit 0 of Reg 1, Bit 16 of raw_inputs) */
    uint16_t regs_ch17[2] = {0x0000, 0x0001};
    zassert_equal(mbsl32di_parse_inputs(regs_ch17, 2U, &reading), 0, "Parse failed");
    zassert_equal(reading.raw_inputs, 0x00010000U, "Channel 17 bit 16 mismatch");
    zassert_true(mbsl32di_get_channel(&reading, 17U), "Channel 17 should be active");
    zassert_false(mbsl32di_get_channel(&reading, 16U), "Channel 16 should be inactive");
    zassert_false(mbsl32di_get_channel(&reading, 18U), "Channel 18 should be inactive");

    /* Channel 32 (Bit 15 of Reg 1, Bit 31 of raw_inputs) */
    uint16_t regs_ch32[2] = {0x0000, 0x8000};
    zassert_equal(mbsl32di_parse_inputs(regs_ch32, 2U, &reading), 0, "Parse failed");
    zassert_equal(reading.raw_inputs, 0x80000000U, "Channel 32 bit 31 mismatch");
    zassert_true(mbsl32di_get_channel(&reading, 32U), "Channel 32 should be active");
    zassert_false(mbsl32di_get_channel(&reading, 31U), "Channel 31 should be inactive");

    /* Mixed pattern: 0x1234, 0x5678 -> 0x56781234 */
    uint16_t regs_mixed[2] = {0x1234, 0x5678};
    zassert_equal(mbsl32di_parse_inputs(regs_mixed, 2U, &reading), 0, "Parse failed");
    zassert_equal(reading.raw_inputs, 0x56781234U, "Mixed pattern mismatch");
}

ZTEST(mbsl32di_codec_suite, test_get_channel_validation)
{
    mbsl32di_reading_t reading = {
        .raw_inputs = 0xFFFFFFFFU,
        .is_valid = true,
    };

    /* Channel out-of-range checks */
    zassert_false(mbsl32di_get_channel(&reading, 0U), "Channel 0 must be rejected");
    zassert_false(mbsl32di_get_channel(&reading, 33U), "Channel 33 must be rejected");
    zassert_false(mbsl32di_get_channel(&reading, 255U), "Channel 255 must be rejected");

    /* NULL and invalid readings */
    zassert_false(mbsl32di_get_channel(NULL, 1U), "NULL reading must return false");

    reading.is_valid = false;
    zassert_false(mbsl32di_get_channel(&reading, 1U), "Invalid reading must return false");
}

/*============================================================================*
 * CONFIGURATION REGISTER PARSING TESTS
 *============================================================================*/

ZTEST(mbsl32di_codec_suite, test_parse_config_rejects_bad_arguments)
{
    uint16_t regs[5] = {3U, 2U, 1U, 60U, 100U};
    mbsl32di_config_t cfg;

    zassert_equal(mbsl32di_parse_config(NULL, 5U, &cfg), -EINVAL, "NULL regs must fail");
    zassert_equal(mbsl32di_parse_config(regs, 5U, NULL), -EINVAL, "NULL out_cfg must fail");
    zassert_equal(mbsl32di_parse_config(regs, 3U, &cfg), -EINVAL, "Short buffer (< 4 regs) must fail");

    /* Invalid slave address */
    regs[0] = 0U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Address 0 must fail");
    regs[0] = 256U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Address 256 must fail");
    regs[0] = 3U;

    /* Invalid baud code */
    regs[1] = 0U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Baud code 0 must fail");
    regs[1] = 7U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Baud code 7 must fail");
    regs[1] = 2U;

    /* Invalid parity code */
    regs[2] = 0U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Parity code 0 must fail");
    regs[2] = 4U;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), -EINVAL, "Parity code 4 must fail");
}

ZTEST(mbsl32di_codec_suite, test_parse_config_baudrate_mappings)
{
    uint16_t regs[5] = {1U, 0U, 1U, 0U, 0x0102U};
    mbsl32di_config_t cfg;

    const struct {
        uint16_t code;
        uint32_t expected_baud;
    } bauds[] = {
        {MBSL32DI_BAUD_CODE_4800, 4800U},
        {MBSL32DI_BAUD_CODE_9600, 9600U},
        {MBSL32DI_BAUD_CODE_19200, 19200U},
        {MBSL32DI_BAUD_CODE_38400, 38400U},
        {MBSL32DI_BAUD_CODE_57600, 57600U},
        {MBSL32DI_BAUD_CODE_115200, 115200U},
    };

    for (size_t i = 0; i < ARRAY_SIZE(bauds); i++) {
        regs[1] = bauds[i].code;
        zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), 0, "Baud code %u failed", bauds[i].code);
        zassert_equal(cfg.baudrate, bauds[i].expected_baud, "Baudrate mismatch for code %u", bauds[i].code);
    }
}

ZTEST(mbsl32di_codec_suite, test_parse_config_parity_mappings)
{
    uint16_t regs[5] = {1U, 2U, 0U, 120U, 0x0201U};
    mbsl32di_config_t cfg;

    regs[2] = MBSL32DI_PARITY_NONE;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), 0, "Parity none failed");
    zassert_equal(cfg.parity, MBSL32DI_PARITY_NONE, "Parity none mismatch");

    regs[2] = MBSL32DI_PARITY_ODD;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), 0, "Parity odd failed");
    zassert_equal(cfg.parity, MBSL32DI_PARITY_ODD, "Parity odd mismatch");

    regs[2] = MBSL32DI_PARITY_EVEN;
    zassert_equal(mbsl32di_parse_config(regs, 5U, &cfg), 0, "Parity even failed");
    zassert_equal(cfg.parity, MBSL32DI_PARITY_EVEN, "Parity even mismatch");
}

ZTEST(mbsl32di_codec_suite, test_parse_config_optional_version_register)
{
    mbsl32di_config_t cfg;

    /* 4 registers provided (no version register) */
    uint16_t regs4[4] = {5U, MBSL32DI_BAUD_CODE_9600, MBSL32DI_PARITY_NONE, 1800U};
    zassert_equal(mbsl32di_parse_config(regs4, 4U, &cfg), 0, "4 regs parse failed");
    zassert_equal(cfg.slave_addr, 5U, "Slave address mismatch");
    zassert_equal(cfg.baudrate, 9600U, "Baudrate mismatch");
    zassert_equal(cfg.parity, MBSL32DI_PARITY_NONE, "Parity mismatch");
    zassert_equal(cfg.timeout_s, 1800U, "Timeout mismatch");
    zassert_equal(cfg.version, 0U, "Version should be 0 when omitted");

    /* 5 registers provided (with version register) */
    uint16_t regs5[5] = {5U, MBSL32DI_BAUD_CODE_9600, MBSL32DI_PARITY_NONE, 1800U, 0x0310U};
    zassert_equal(mbsl32di_parse_config(regs5, 5U, &cfg), 0, "5 regs parse failed");
    zassert_equal(cfg.version, 0x0310U, "Version mismatch");
}

/*============================================================================*
 * CONFIGURATION REGISTER BUILDING TESTS
 *============================================================================*/

ZTEST(mbsl32di_codec_suite, test_build_config_registers_rejects_bad_arguments)
{
    uint16_t out_regs[4];

    zassert_equal(mbsl32di_build_config_registers(1U, 9600U, MBSL32DI_PARITY_NONE, 60U, NULL, 4U),
                  -EINVAL, "NULL out_regs must fail");
    zassert_equal(mbsl32di_build_config_registers(1U, 9600U, MBSL32DI_PARITY_NONE, 60U, out_regs, 3U),
                  -EINVAL, "Buffer < 4 words must fail");
    zassert_equal(mbsl32di_build_config_registers(0U, 9600U, MBSL32DI_PARITY_NONE, 60U, out_regs, 4U),
                  -EINVAL, "Slave address 0 must fail");
    zassert_equal(mbsl32di_build_config_registers(1U, 1200U, MBSL32DI_PARITY_NONE, 60U, out_regs, 4U),
                  -EINVAL, "Unsupported baud 1200 must fail");
    zassert_equal(mbsl32di_build_config_registers(1U, 9600U, 0U, 60U, out_regs, 4U),
                  -EINVAL, "Invalid parity 0 must fail");
    zassert_equal(mbsl32di_build_config_registers(1U, 9600U, 4U, 60U, out_regs, 4U),
                  -EINVAL, "Invalid parity 4 must fail");
}

ZTEST(mbsl32di_codec_suite, test_build_config_registers_success_and_roundtrip)
{
    uint16_t out_regs[4];

    const struct {
        uint8_t slave_addr;
        uint32_t baudrate;
        uint8_t parity;
        uint16_t timeout_s;
        uint16_t expected_baud_code;
    } test_vectors[] = {
        {3U, 9600U, MBSL32DI_PARITY_NONE, 0U, MBSL32DI_BAUD_CODE_9600},
        {10U, 115200U, MBSL32DI_PARITY_EVEN, 1800U, MBSL32DI_BAUD_CODE_115200},
        {1U, 4800U, MBSL32DI_PARITY_ODD, 30U, MBSL32DI_BAUD_CODE_4800},
        {255U, 19200U, MBSL32DI_PARITY_NONE, 60U, MBSL32DI_BAUD_CODE_19200},
        {16U, 38400U, MBSL32DI_PARITY_NONE, 120U, MBSL32DI_BAUD_CODE_38400},
        {42U, 57600U, MBSL32DI_PARITY_EVEN, 300U, MBSL32DI_BAUD_CODE_57600},
    };

    for (size_t i = 0; i < ARRAY_SIZE(test_vectors); i++) {
        memset(out_regs, 0, sizeof(out_regs));
        int res = mbsl32di_build_config_registers(test_vectors[i].slave_addr,
                                                  test_vectors[i].baudrate,
                                                  test_vectors[i].parity,
                                                  test_vectors[i].timeout_s,
                                                  out_regs, 4U);
        zassert_equal(res, 0, "Build config failed for index %zu", i);
        zassert_equal(out_regs[0], (uint16_t)test_vectors[i].slave_addr, "Slave address mismatch");
        zassert_equal(out_regs[1], test_vectors[i].expected_baud_code, "Baud code mismatch");
        zassert_equal(out_regs[2], (uint16_t)test_vectors[i].parity, "Parity code mismatch");
        zassert_equal(out_regs[3], test_vectors[i].timeout_s, "Timeout mismatch");

        /* Roundtrip through parser */
        mbsl32di_config_t parsed_cfg;
        res = mbsl32di_parse_config(out_regs, 4U, &parsed_cfg);
        zassert_equal(res, 0, "Roundtrip parse failed for index %zu", i);
        zassert_equal(parsed_cfg.slave_addr, test_vectors[i].slave_addr, "Roundtrip addr mismatch");
        zassert_equal(parsed_cfg.baudrate, test_vectors[i].baudrate, "Roundtrip baud mismatch");
        zassert_equal(parsed_cfg.parity, test_vectors[i].parity, "Roundtrip parity mismatch");
        zassert_equal(parsed_cfg.timeout_s, test_vectors[i].timeout_s, "Roundtrip timeout mismatch");
    }
}
