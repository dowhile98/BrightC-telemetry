/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * TDD Unit Tests for BrightC Pure C Libraries (Filter, Transducers, Modbus Codec).
 */

#include <zephyr/ztest.h>
#include <math.h>

#include "filter_ema.h"
#include "filter_median.h"
#include "transducer_rtd.h"
#include "transducer_current_loop.h"
#include "transducer_voltage_divider.h"
#include "transducer_linear.h"
#include "modbus_decode.h"

/* =========================================================================
 * 1. Filter Tests
 * ========================================================================= */
ZTEST_SUITE(filter_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(filter_suite, test_ema_initialization_and_first_sample)
{
    filter_ema_t f;
    filter_ema_init(&f, 0.2f);
    
    zassert_false(f.is_initialized, "Filter should not be initialized initially");
    zassert_equal(f.alpha, 0.2f, "Alpha should be set to 0.2");

    /* First sample must seed the filter directly (no startup transient) */
    float out = filter_ema_update(&f, 50.0f);
    zassert_true(f.is_initialized, "Filter should be initialized after first sample");
    zassert_within(out, 50.0f, 0.001f, "First output must equal first input");

    /* Second sample: out = 0.2 * 100.0 + 0.8 * 50.0 = 20 + 40 = 60.0 */
    out = filter_ema_update(&f, 100.0f);
    zassert_within(out, 60.0f, 0.001f, "Expected EMA weighted value 60.0");
}

ZTEST(filter_suite, test_median_filter_spike_rejection)
{
    filter_median_t med;
    zassert_equal(filter_median_init(&med, 5), 0, "Median filter init failed");

    /* Fill with constant 25.0 */
    for (int i = 0; i < 5; i++) {
        filter_median_update(&med, 25.0f);
    }
    zassert_within(filter_median_update(&med, 25.0f), 25.0f, 0.001f, "Constant 25.0 expected");

    /* Introduce a massive single-sample inductive spike (1000.0) */
    float out = filter_median_update(&med, 1000.0f);
    zassert_within(out, 25.0f, 0.001f, "Median filter must completely reject impulse spike!");
}

/* =========================================================================
 * 2. Transducer Tests
 * ========================================================================= */
ZTEST_SUITE(transducer_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(transducer_suite, test_rtd_callendar_van_dusen)
{
    float temp_c = 0.0f;
    /* IEC 60751: PT100 at 0 deg C is exactly 100.0 Ohms */
    int err = transducer_rtd_ohms_to_celsius(100.0f, 100.0f, &temp_c);
    zassert_equal(err, 0, "RTD conversion failed for 0C");
    zassert_within(temp_c, 0.0f, 0.05f, "Expected 0.0 deg C at 100 Ohms");

    /* PT100 at 100 deg C is ~138.51 Ohms */
    err = transducer_rtd_ohms_to_celsius(100.0f, 138.51f, &temp_c);
    zassert_equal(err, 0, "RTD conversion failed for 100C");
    zassert_within(temp_c, 100.0f, 0.1f, "Expected ~100.0 deg C at 138.51 Ohms");
}

ZTEST(transducer_suite, test_current_loop_and_namur_ne43)
{
    /* Test NAMUR NE43 status classification */
    zassert_equal(transducer_current_loop_diagnose_namur(2.0f), 
                  NAMUR_STATUS_FAILURE_LOW_WIRE_BREAK, "2 mA must be wire break");
    zassert_equal(transducer_current_loop_diagnose_namur(12.0f), 
                  NAMUR_STATUS_NORMAL, "12 mA must be normal");
    zassert_equal(transducer_current_loop_diagnose_namur(22.5f), 
                  NAMUR_STATUS_FAILURE_HIGH_SHORT_CIRCUIT, "22.5 mA must be short circuit");

    /* Test 4-20 mA to 0-600 Bar scaling */
    current_loop_scale_cfg_t scale = {
        .ma_at_min = 4.0f,
        .physical_min = 0.0f,
        .ma_at_max = 20.0f,
        .physical_max = 600.0f,
    };

    float pressure = 0.0f;
    zassert_equal(transducer_current_loop_ma_to_physical(&scale, 4.0f, &pressure), 0, "Scaling failed");
    zassert_within(pressure, 0.0f, 0.01f, "4 mA should map to 0 Bar");

    zassert_equal(transducer_current_loop_ma_to_physical(&scale, 20.0f, &pressure), 0, "Scaling failed");
    zassert_within(pressure, 600.0f, 0.01f, "20 mA should map to 600 Bar");

    zassert_equal(transducer_current_loop_ma_to_physical(&scale, 12.0f, &pressure), 0, "Scaling failed");
    zassert_within(pressure, 300.0f, 0.01f, "12 mA should map to 300 Bar (midpoint)");
}

ZTEST(transducer_suite, test_voltage_divider)
{
    /* 10k / 10k divider (divide by 2) with 3.3V reference and 16-bit ADC */
    voltage_divider_cfg_t vdiv = {
        .r_high_ohms = 10000.0f,
        .r_low_ohms = 10000.0f,
        .v_ref = 3.3f,
        .adc_max = 65535.0f,
    };

    float v_out = 0.0f;
    /* Half scale: 32767 counts -> Vpin = 1.65V -> Vin = 3.3V */
    int err = transducer_voltage_divider_adc_to_volts(&vdiv, 32767.5f, &v_out);
    zassert_equal(err, 0, "Voltage divider calculation failed");
    zassert_within(v_out, 3.3f, 0.01f, "Expected 3.3V input for half-scale divider");
}

/* =========================================================================
 * 3. Modbus Codec Tests
 * ========================================================================= */
ZTEST_SUITE(modbus_codec_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(modbus_codec_suite, test_float32_decoding)
{
    /* 100.0f in IEEE 754 hex is 0x42C80000 */
    uint16_t msw = 0x42C8;
    uint16_t lsw = 0x0000;

    float decoded = 0.0f;
    /* Big Endian (MSW first): reg0=msw, reg1=lsw */
    int err = modbus_decode_float32(msw, lsw, MODBUS_ENDIANNESS_BE, &decoded);
    zassert_equal(err, 0, "Float32 BE decode failed");
    zassert_within(decoded, 100.0f, 0.001f, "Expected 100.0f from BE registers");

    /* Little Endian (LSW first): reg0=lsw, reg1=msw */
    err = modbus_decode_float32(lsw, msw, MODBUS_ENDIANNESS_LE, &decoded);
    zassert_equal(err, 0, "Float32 LE decode failed");
    zassert_within(decoded, 100.0f, 0.001f, "Expected 100.0f from LE registers");
}

ZTEST(modbus_codec_suite, test_scaled_integer_decoding)
{
    /* Digital temperature sensor sending 245 with 0.1 scale factor = 24.5 deg C */
    uint16_t reg = 245;
    float decoded = 0.0f;
    int err = modbus_decode_int16_scaled(reg, 0.1f, &decoded);
    zassert_equal(err, 0, "Scaled int16 decode failed");
    zassert_within(decoded, 24.5f, 0.001f, "Expected 24.5 from scaled integer");

    /* Negative temperature: -150 * 0.1 = -15.0 deg C (int16_t -150 is 0xFF6A) */
    uint16_t neg_reg = (uint16_t)(-150);
    err = modbus_decode_int16_scaled(neg_reg, 0.1f, &decoded);
    zassert_equal(err, 0, "Negative scaled int16 decode failed");
    zassert_within(decoded, -15.0f, 0.001f, "Expected -15.0 from negative integer");
}
