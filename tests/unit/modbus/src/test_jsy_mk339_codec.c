/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the JSY-MK-339 energy meter register codec and port contracts.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "domain/services/jsy_mk339_codec.h"
#include "interfaces/i_energy_meter.h"
#include "fake_energy_meter.h"

ZTEST_SUITE(jsy_mk339_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(jsy_mk339_suite, test_jsy_mk339_parse_electrical_parameters_full_block)
{
    uint16_t regs[JSY_MK339_ELECTRICAL_FULL_REG_COUNT];
    memset(regs, 0, sizeof(regs));

    /* Voltages: 0x0048..0x004A (/ 100) */
    regs[0] = 23000; /* Va = 230.00 V */
    regs[1] = 23150; /* Vb = 231.50 V */
    regs[2] = 22980; /* Vc = 229.80 V */

    /* Currents: 0x004B..0x004D (/ 100) */
    regs[3] = 1050;  /* Ia = 10.50 A */
    regs[4] = 1020;  /* Ib = 10.20 A */
    regs[5] = 1080;  /* Ic = 10.80 A */

    /* Leakage current: 0x004E (/ 10000) */
    regs[6] = 150;   /* I_leak = 0.0150 A */

    /* Active powers: 0x004F..0x0052 (/ 100, kW) */
    regs[7] = 241;   /* Pa = 2.41 kW */
    regs[8] = 236;   /* Pb = 2.36 kW */
    regs[9] = 248;   /* Pc = 2.48 kW */
    regs[10] = 725;  /* P_total = 7.25 kW */

    /* Reactive powers: 0x0053..0x0056 (/ 100, kVar) */
    regs[11] = 31;   /* Qa = 0.31 kVar */
    regs[12] = 30;   /* Qb = 0.30 kVar */
    regs[13] = 32;   /* Qc = 0.32 kVar */
    regs[14] = 93;   /* Q_total = 0.93 kVar */

    /* Apparent powers: 0x0057..0x005A (/ 100, kVA) */
    regs[15] = 243;  /* Sa = 2.43 kVA */
    regs[16] = 238;  /* Sb = 2.38 kVA */
    regs[17] = 250;  /* Sc = 2.50 kVA */
    regs[18] = 731;  /* S_total = 7.31 kVA */

    /* Power factors: 0x005B..0x005E (/ 1000) */
    regs[19] = 992;  /* PFa = 0.992 */
    regs[20] = 991;  /* PFb = 0.991 */
    regs[21] = 992;  /* PFc = 0.992 */
    regs[22] = 992;  /* PF_total = 0.992 */

    /* Active energies: 0x005F..0x0066 (U32 BE / 100, kWh) */
    /* Phase A: 12345.67 kWh -> raw = 1234567 = 0x0012D687 */
    regs[23] = 0x0012;
    regs[24] = 0xD687;
    /* Phase B: 20000.00 kWh -> raw = 2000000 = 0x001E8480 */
    regs[25] = 0x001E;
    regs[26] = 0x8480;
    /* Phase C: 30000.00 kWh -> raw = 3000000 = 0x002DC6C0 */
    regs[27] = 0x002D;
    regs[28] = 0xC6C0;
    /* Total: 62345.67 kWh -> raw = 6234567 = 0x005F21C7 */
    regs[29] = 0x005F;
    regs[30] = 0x21C7;

    /* Reactive energies: 0x0067..0x006E (U32 BE / 100, kvarh) */
    /* Total: 5000.00 kvarh -> raw = 500000 = 0x0007A120 */
    regs[37] = 0x0007;
    regs[38] = 0xA120;

    /* Apparent energies: 0x006F..0x0076 (U32 BE / 100, kVAh) */
    /* Total: 6500.00 kVAh -> raw = 650000 = 0x0009EB10 */
    regs[45] = 0x0009;
    regs[46] = 0xEB10;

    /* Frequency: 0x0077 (/ 100, Hz) */
    regs[47] = 5000; /* 50.00 Hz */

    energy_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    int err = jsy_mk339_parse_electrical_parameters(regs, JSY_MK339_ELECTRICAL_FULL_REG_COUNT,
                                                    &reading);
    zassert_equal(err, 0, "Parser returned error: %d", err);
    zassert_true(reading.is_valid, "Reading must be valid");

    /* Voltages */
    zassert_within(reading.voltage_v[ENERGY_PHASE_A], 230.00f, 0.01f, "Va mismatch");
    zassert_within(reading.voltage_v[ENERGY_PHASE_B], 231.50f, 0.01f, "Vb mismatch");
    zassert_within(reading.voltage_v[ENERGY_PHASE_C], 229.80f, 0.01f, "Vc mismatch");

    /* Currents */
    zassert_within(reading.current_a[ENERGY_PHASE_A], 10.50f, 0.01f, "Ia mismatch");
    zassert_within(reading.current_a[ENERGY_PHASE_B], 10.20f, 0.01f, "Ib mismatch");
    zassert_within(reading.current_a[ENERGY_PHASE_C], 10.80f, 0.01f, "Ic mismatch");
    zassert_within(reading.leakage_current_a, 0.0150f, 0.0001f, "Leakage current mismatch");

    /* Active powers */
    zassert_within(reading.active_power_kw[ENERGY_PHASE_A], 2.41f, 0.01f, "Pa mismatch");
    zassert_within(reading.active_power_kw[ENERGY_PHASE_B], 2.36f, 0.01f, "Pb mismatch");
    zassert_within(reading.active_power_kw[ENERGY_PHASE_C], 2.48f, 0.01f, "Pc mismatch");
    zassert_within(reading.total_active_power_kw, 7.25f, 0.01f, "Total active power mismatch");

    /* Reactive powers */
    zassert_within(reading.reactive_power_kvar[ENERGY_PHASE_A], 0.31f, 0.01f, "Qa mismatch");
    zassert_within(reading.total_reactive_power_kvar, 0.93f, 0.01f, "Total reactive power mismatch");

    /* Apparent powers */
    zassert_within(reading.apparent_power_kva[ENERGY_PHASE_A], 2.43f, 0.01f, "Sa mismatch");
    zassert_within(reading.total_apparent_power_kva, 7.31f, 0.01f, "Total apparent power mismatch");

    /* Power factors */
    zassert_within(reading.power_factor[ENERGY_PHASE_A], 0.992f, 0.001f, "PFa mismatch");
    zassert_within(reading.total_power_factor, 0.992f, 0.001f, "Total PF mismatch");

    /* Active energies */
    zassert_within(reading.active_energy_kwh[ENERGY_PHASE_A], 12345.67, 0.01, "Ea mismatch");
    zassert_within(reading.active_energy_kwh[ENERGY_PHASE_B], 20000.00, 0.01, "Eb mismatch");
    zassert_within(reading.active_energy_kwh[ENERGY_PHASE_C], 30000.00, 0.01, "Ec mismatch");
    zassert_within(reading.total_active_energy_kwh, 62345.67, 0.01, "Total active energy mismatch");

    /* Reactive energy */
    zassert_within(reading.total_reactive_energy_kvarh, 5000.00, 0.01, "Total reactive energy mismatch");

    /* Apparent energy */
    zassert_within(reading.total_apparent_energy_kvah, 6500.00, 0.01, "Total apparent energy mismatch");

    /* Frequency */
    zassert_within(reading.frequency_hz, 50.00f, 0.01f, "Frequency mismatch");
}

ZTEST(jsy_mk339_suite, test_jsy_mk339_parse_electrical_parameters_basic_block)
{
    uint16_t regs[JSY_MK339_ELECTRICAL_BASIC_REG_COUNT];
    memset(regs, 0, sizeof(regs));

    regs[0] = 22000; /* Va = 220.00 V */
    regs[3] = 500;   /* Ia = 5.00 A */
    regs[10] = 110;  /* P_total = 1.10 kW */
    regs[29] = 0x0001; /* Total energy: 1000.00 kWh (raw 100000 = 0x000186A0) */
    regs[30] = 0x86A0;

    energy_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    int err = jsy_mk339_parse_electrical_parameters(regs, JSY_MK339_ELECTRICAL_BASIC_REG_COUNT,
                                                    &reading);
    zassert_equal(err, 0, "Basic block parser failed");
    zassert_true(reading.is_valid, "Reading must be valid");
    zassert_within(reading.voltage_v[ENERGY_PHASE_A], 220.00f, 0.01f, "Va mismatch");
    zassert_within(reading.current_a[ENERGY_PHASE_A], 5.00f, 0.01f, "Ia mismatch");
    zassert_within(reading.total_active_power_kw, 1.10f, 0.01f, "Total P mismatch");
    zassert_within(reading.total_active_energy_kwh, 1000.00, 0.01, "Total E mismatch");
    zassert_equal(reading.frequency_hz, 0.0f, "Frequency should be 0 when not in basic block");
}

ZTEST(jsy_mk339_suite, test_jsy_mk339_parse_rejects_invalid_arguments)
{
    uint16_t regs[JSY_MK339_ELECTRICAL_BASIC_REG_COUNT];
    energy_reading_t reading;

    zassert_equal(jsy_mk339_parse_electrical_parameters(NULL, JSY_MK339_ELECTRICAL_BASIC_REG_COUNT,
                                                        &reading),
                  -EINVAL, "NULL regs must be rejected");
    zassert_equal(jsy_mk339_parse_electrical_parameters(regs, JSY_MK339_ELECTRICAL_BASIC_REG_COUNT,
                                                        NULL),
                  -EINVAL, "NULL output must be rejected");
    zassert_equal(jsy_mk339_parse_electrical_parameters(regs, JSY_MK339_ELECTRICAL_BASIC_REG_COUNT - 1,
                                                        &reading),
                  -EINVAL, "Short buffer must be rejected");
}

ZTEST(jsy_mk339_suite, test_jsy_mk339_config_register_build_and_parse)
{
    uint16_t reg = 0U;

    /* Build: addr 1, 9600 bps -> 0x0106 */
    int err = jsy_mk339_build_config_register(1U, 9600U, &reg);
    zassert_equal(err, 0, "Build 9600 failed");
    zassert_equal(reg, 0x0106, "Expected 0x0106 for addr 1, 9600");

    uint8_t out_addr = 0U;
    uint32_t out_baud = 0U;
    err = jsy_mk339_parse_config_register(reg, &out_addr, &out_baud);
    zassert_equal(err, 0, "Parse 0x0106 failed");
    zassert_equal(out_addr, 1U, "Slave addr mismatch");
    zassert_equal(out_baud, 9600U, "Baudrate mismatch");

    /* Build: addr 2, 19200 bps -> 0x0207 */
    err = jsy_mk339_build_config_register(2U, 19200U, &reg);
    zassert_equal(err, 0, "Build 19200 failed");
    zassert_equal(reg, 0x0207, "Expected 0x0207 for addr 2, 19200");

    err = jsy_mk339_parse_config_register(reg, &out_addr, &out_baud);
    zassert_equal(err, 0, "Parse 0x0207 failed");
    zassert_equal(out_addr, 2U, "Slave addr mismatch");
    zassert_equal(out_baud, 19200U, "Baudrate mismatch");

    /* Build: addr 10, 4800 bps -> 0x0A05 */
    err = jsy_mk339_build_config_register(10U, 4800U, &reg);
    zassert_equal(err, 0, "Build 4800 failed");
    zassert_equal(reg, 0x0A05, "Expected 0x0A05 for addr 10, 4800");

    err = jsy_mk339_parse_config_register(reg, &out_addr, &out_baud);
    zassert_equal(err, 0, "Parse 0x0A05 failed");
    zassert_equal(out_addr, 10U, "Slave addr mismatch");
    zassert_equal(out_baud, 4800U, "Baudrate mismatch");
}

ZTEST(jsy_mk339_suite, test_jsy_mk339_config_register_rejects_invalid)
{
    uint16_t reg = 0U;
    uint8_t out_addr = 0U;
    uint32_t out_baud = 0U;

    zassert_equal(jsy_mk339_build_config_register(0U, 9600U, &reg), -EINVAL,
                  "Addr 0 must be rejected");
    zassert_equal(jsy_mk339_build_config_register(1U, 9600U, NULL), -EINVAL,
                  "NULL out_reg must be rejected");
    zassert_equal(jsy_mk339_build_config_register(1U, 115200U, &reg), -EINVAL,
                  "Unsupported baud must be rejected");

    zassert_equal(jsy_mk339_parse_config_register(0x0106, NULL, &out_baud), -EINVAL,
                  "NULL addr out must be rejected");
    zassert_equal(jsy_mk339_parse_config_register(0x0106, &out_addr, NULL), -EINVAL,
                  "NULL baud out must be rejected");
    zassert_equal(jsy_mk339_parse_config_register(0x0100, &out_addr, &out_baud), -EINVAL,
                  "Invalid baud code must be rejected");
}

ZTEST(jsy_mk339_suite, test_fake_energy_meter_port)
{
    /* Test unbound port validation per rule §4.1.D */
    i_energy_meter_t unbound = {0};
    energy_reading_t reading;
    memset(&reading, 0, sizeof(reading));

    zassert_equal(i_energy_meter_get_reading(&unbound, &reading), -EINVAL,
                  "Unbound handle must return -EINVAL");
    zassert_false(i_energy_meter_is_bound(&unbound), "Unbound port should report false");

    /* Setup fake */
    fake_energy_meter_reset();
    i_energy_meter_t port = fake_energy_meter_get_port();
    zassert_true(i_energy_meter_is_bound(&port), "Fake port must be bound");

    /* Null output */
    zassert_equal(i_energy_meter_get_reading(&port, NULL), -EINVAL,
                  "NULL output buffer must return -EINVAL");

    /* Set simulated reading */
    energy_reading_t sim_reading;
    memset(&sim_reading, 0, sizeof(sim_reading));
    sim_reading.device_id = 2U;
    sim_reading.total_active_power_kw = 15.75f;
    sim_reading.total_active_energy_kwh = 12345.6;
    sim_reading.is_valid = true;

    fake_energy_meter_set_reading(&sim_reading);

    zassert_equal(i_energy_meter_get_reading(&port, &reading), 0, "Get reading failed");
    zassert_equal(reading.device_id, 2U, "Device ID mismatch");
    zassert_within(reading.total_active_power_kw, 15.75f, 0.01f, "Active power mismatch");
    zassert_within(reading.total_active_energy_kwh, 12345.6, 0.01, "Active energy mismatch");
    zassert_true(reading.is_valid, "Validity mismatch");

    const fake_energy_meter_state_t *state = fake_energy_meter_get_state();
    zassert_equal(state->get_reading_calls, 1U, "Calls counter mismatch");

    /* Error override */
    fake_energy_meter_set_return_code(-EIO);
    zassert_equal(i_energy_meter_get_reading(&port, &reading), -EIO,
                  "Return code override failed");
}
