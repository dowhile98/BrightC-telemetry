/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adapter_snmp_telemetry.h"

#if defined(CONFIG_SNMP_AGENT)

#include <zephyr/logging/log.h>
#include <snmp/snmp.h>
#include <snmp/snmp_mib.h>
#include <stdio.h>
#include <string.h>

#include "application/services/app_facade.h"
#include "common/sensor_types.h"
#include "common/energy_types.h"
#include "common/thermal_energy_types.h"

LOG_MODULE_REGISTER(adapter_snmp_telemetry, LOG_LEVEL_INF);

#define DAECGE_ENTERPRISE_ID 3134U

/* Base OID: 1.3.6.1.4.1.3134.2.3 */
#define SNMP_OID_BRIGHTCV3_BASE 1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3

/* =========================================================================
 * Process Temperatures Callback (1.3.6.1.4.1.3134.2.3.2.2.<subid>.0)
 * ========================================================================= */

static int cb_get_process_temp(struct snmp_agent *agent, const struct snmp_mib_node *node,
                               struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    if (node == NULL || vb == NULL || node->oid.len < 13)
    {
        return -EINVAL;
    }

    uint32_t subid = node->oid.ids[node->oid.len - 2];
    thermal_energy_capture_t capture;
    memset(&capture, 0, sizeof(capture));
    bool has_capture = (app_facade_thermal_get_capture(&capture) == 0);

    /* Humidity Return Grille (subid 16) is Unsigned32 / Gauge32 */
    if (subid == 16)
    {
        vb->type = SNMP_TAG_GAUGE32;
        if (has_capture && capture.humidity_return_pct >= 0.0f)
        {
            vb->val.uint_val = (uint32_t)(capture.humidity_return_pct * 10.0f + 0.5f);
        }
        else
        {
            vb->val.uint_val = 0U;
        }
        return 0;
    }

    vb->type = ASN1_TAG_INTEGER;
    int32_t temp_x10 = -9999; /* Sentinel for invalid / not available */

    /* HVAC Group Control Temperatures (subids 17..20 -> Salas 1..4) */
    if (subid >= 17 && subid <= 20)
    {
        uint8_t g = (uint8_t)(subid - 17U);
        app_facade_hvac_status_t hstat;
        memset(&hstat, 0, sizeof(hstat));
        if (app_facade_hvac_get_status(&hstat) == 0 && g < hstat.total_groups)
        {
            float t = hstat.groups[g].highest_temp;
            if (t > -100.0f && t < 150.0f)
            {
                temp_x10 = (int32_t)(t * 10.0f + (t >= 0.0f ? 0.5f : -0.5f));
            }
        }
        vb->val.int_val = temp_x10;
        return 0;
    }

    if (has_capture)
    {
        switch (subid)
        {
        case 1: /* tempSupplyEvap1 */
            if ((capture.inj_valid_mask & (1U << 0)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[0] * 10.0f + (capture.t_inj[0] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 2: /* tempReturnEvap1 */
            if ((capture.ret_valid_mask & (1U << 0)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[0] * 10.0f + (capture.t_ret[0] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 3: /* tempDeltaEvap1 */
            if ((capture.pair_valid_mask & (1U << 0)) != 0U)
            {
                float delta = capture.t_ret[0] - capture.t_inj[0];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 4: /* tempSupplyEvap2 */
            if ((capture.inj_valid_mask & (1U << 1)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[1] * 10.0f + (capture.t_inj[1] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 5: /* tempReturnEvap2 */
            if ((capture.ret_valid_mask & (1U << 1)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[1] * 10.0f + (capture.t_ret[1] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 6: /* tempDeltaEvap2 */
            if ((capture.pair_valid_mask & (1U << 1)) != 0U)
            {
                float delta = capture.t_ret[1] - capture.t_inj[1];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 7: /* tempSupplyEvap3 */
            if ((capture.inj_valid_mask & (1U << 2)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[2] * 10.0f + (capture.t_inj[2] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 8: /* tempReturnEvap3 */
            if ((capture.ret_valid_mask & (1U << 2)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[2] * 10.0f + (capture.t_ret[2] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 9: /* tempDeltaEvap3 */
            if ((capture.pair_valid_mask & (1U << 2)) != 0U)
            {
                float delta = capture.t_ret[2] - capture.t_inj[2];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 10: /* tempSupplyEvap4 */
            if ((capture.inj_valid_mask & (1U << 3)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[3] * 10.0f + (capture.t_inj[3] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 11: /* tempReturnEvap4 */
            if ((capture.ret_valid_mask & (1U << 3)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[3] * 10.0f + (capture.t_ret[3] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 12: /* tempDeltaEvap4 */
            if ((capture.pair_valid_mask & (1U << 3)) != 0U)
            {
                float delta = capture.t_ret[3] - capture.t_inj[3];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 13: /* tempRoomAmbient1 */
            if ((capture.room_valid_mask & (1U << 0)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_room[0] * 10.0f + (capture.t_room[0] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 14: /* tempRoomAmbient2 */
            if ((capture.room_valid_mask & (1U << 1)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_room[1] * 10.0f + (capture.t_room[1] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 15: /* tempOutdoorAmbient1 */
            if ((capture.outdoor_valid_mask & (1U << 0)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_outdoor[0] * 10.0f + (capture.t_outdoor[0] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 21: /* tempSupplyEvap5 */
            if ((capture.inj_valid_mask & (1U << 4)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[4] * 10.0f + (capture.t_inj[4] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 22: /* tempReturnEvap5 */
            if ((capture.ret_valid_mask & (1U << 4)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[4] * 10.0f + (capture.t_ret[4] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 23: /* tempDeltaEvap5 */
            if ((capture.pair_valid_mask & (1U << 4)) != 0U)
            {
                float delta = capture.t_ret[4] - capture.t_inj[4];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 24: /* tempSupplyEvap6 */
            if ((capture.inj_valid_mask & (1U << 5)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[5] * 10.0f + (capture.t_inj[5] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 25: /* tempReturnEvap6 */
            if ((capture.ret_valid_mask & (1U << 5)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[5] * 10.0f + (capture.t_ret[5] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 26: /* tempDeltaEvap6 */
            if ((capture.pair_valid_mask & (1U << 5)) != 0U)
            {
                float delta = capture.t_ret[5] - capture.t_inj[5];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 27: /* tempSupplyEvap7 */
            if ((capture.inj_valid_mask & (1U << 6)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[6] * 10.0f + (capture.t_inj[6] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 28: /* tempReturnEvap7 */
            if ((capture.ret_valid_mask & (1U << 6)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[6] * 10.0f + (capture.t_ret[6] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 29: /* tempDeltaEvap7 */
            if ((capture.pair_valid_mask & (1U << 6)) != 0U)
            {
                float delta = capture.t_ret[6] - capture.t_inj[6];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 30: /* tempSupplyEvap8 */
            if ((capture.inj_valid_mask & (1U << 7)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_inj[7] * 10.0f + (capture.t_inj[7] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 31: /* tempReturnEvap8 */
            if ((capture.ret_valid_mask & (1U << 7)) != 0U)
            {
                temp_x10 = (int32_t)(capture.t_ret[7] * 10.0f + (capture.t_ret[7] >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        case 32: /* tempDeltaEvap8 */
            if ((capture.pair_valid_mask & (1U << 7)) != 0U)
            {
                float delta = capture.t_ret[7] - capture.t_inj[7];
                temp_x10 = (int32_t)(delta * 10.0f + (delta >= 0.0f ? 0.5f : -0.5f));
            }
            break;
        default:
            return -ENOENT;
        }
    }

    vb->val.int_val = temp_x10;
    return 0;
}

/* =========================================================================
 * Energy Meter Callback (1.3.6.1.4.1.3134.2.3.6.<subid>.0)
 * ========================================================================= */

static int cb_get_energy_telemetry(struct snmp_agent *agent, const struct snmp_mib_node *node,
                                   struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    if (node == NULL || vb == NULL || node->oid.len < 12)
    {
        return -EINVAL;
    }

    uint32_t subid = node->oid.ids[node->oid.len - 2];
    energy_reading_t rd;
    memset(&rd, 0, sizeof(rd));
    int rc = app_facade_energy_get_reading(&rd);
    bool valid = (rc == 0 && rd.is_valid);

    switch (subid)
    {
    case 1: /* energyCommStatus: ok(1), commFault(2), unconfigured(3) */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? 1 : 2;
        break;

    case 2: /* energyLineFrequency: tenths of Hz (e.g. 600 = 60.0 Hz) */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.frequency_hz * 10.0f + 0.5f) : 0U;
        break;

    case 3: /* voltagePhaseA: tenths of Volt */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.voltage_v[0] * 10.0f + 0.5f) : 0U;
        break;

    case 4: /* voltagePhaseB: tenths of Volt */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.voltage_v[1] * 10.0f + 0.5f) : 0U;
        break;

    case 5: /* voltagePhaseC: tenths of Volt */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.voltage_v[2] * 10.0f + 0.5f) : 0U;
        break;

    case 6: /* currentPhaseA: tenths of Ampere */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.current_a[0] * 10.0f + 0.5f) : 0U;
        break;

    case 7: /* currentPhaseB: tenths of Ampere */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.current_a[1] * 10.0f + 0.5f) : 0U;
        break;

    case 8: /* currentPhaseC: tenths of Ampere */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.current_a[2] * 10.0f + 0.5f) : 0U;
        break;

    case 9: /* leakageCurrent: tenths of Ampere */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.leakage_current_a * 10.0f + 0.5f) : 0U;
        break;

    case 10: /* activePowerPhaseA: Watts */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.active_power_kw[0] * 1000.0f +
                                            (rd.active_power_kw[0] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 11: /* activePowerPhaseB: Watts */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.active_power_kw[1] * 1000.0f +
                                            (rd.active_power_kw[1] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 12: /* activePowerPhaseC: Watts */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.active_power_kw[2] * 1000.0f +
                                            (rd.active_power_kw[2] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 13: /* totalActivePower: Watts */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.total_active_power_kw * 1000.0f +
                                            (rd.total_active_power_kw >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 14: /* totalReactivePower: VAr */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.total_reactive_power_kvar * 1000.0f +
                                            (rd.total_reactive_power_kvar >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 15: /* totalApparentPower: VA */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.total_apparent_power_kva * 1000.0f +
                                            (rd.total_apparent_power_kva >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 16: /* powerFactorPhaseA: -100..100 */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.power_factor[0] * 100.0f +
                                            (rd.power_factor[0] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 17: /* powerFactorPhaseB: -100..100 */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.power_factor[1] * 100.0f +
                                            (rd.power_factor[1] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 18: /* powerFactorPhaseC: -100..100 */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.power_factor[2] * 100.0f +
                                            (rd.power_factor[2] >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 19: /* totalPowerFactor: -100..100 */
        vb->type = ASN1_TAG_INTEGER;
        vb->val.int_val = valid ? (int32_t)(rd.total_power_factor * 100.0f +
                                            (rd.total_power_factor >= 0.0f ? 0.5f : -0.5f)) : 0;
        break;

    case 20: /* totalActiveEnergyKwh: kWh */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.total_active_energy_kwh + 0.5) : 0U;
        break;

    case 21: /* totalReactiveEnergyKvarh: kvarh */
        vb->type = SNMP_TAG_GAUGE32;
        vb->val.uint_val = valid ? (uint32_t)(rd.total_reactive_energy_kvarh + 0.5) : 0U;
        break;

    default:
        return -ENOENT;
    }

    return 0;
}

/* =========================================================================
 * Sensor Channels Table Callbacks (1.3.6.1.4.1.3134.2.3.2.1.1.<col>.<row>)
 * ========================================================================= */

static int cb_table_sensor_col1_id(struct snmp_agent *agent, const struct snmp_table_row *row,
                                   uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_INTEGER;
    vb->val.int_val = (int32_t)row->index;
    return 0;
}

static int cb_table_sensor_col2_name(struct snmp_agent *agent, const struct snmp_table_row *row,
                                     uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_OCTET_STRING;
    snprintf((char *)vb->val.octet_str.data, sizeof(vb->val.octet_str.data),
             "CWT1_CH%02u", (unsigned int)row->index);
    vb->val.octet_str.len = (uint16_t)strlen((char *)vb->val.octet_str.data);
    return 0;
}

static int cb_table_sensor_col3_phys_ch(struct snmp_agent *agent, const struct snmp_table_row *row,
                                        uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_INTEGER;
    vb->val.int_val = (int32_t)(row->index - 1U);
    return 0;
}

static int cb_table_sensor_col4_role(struct snmp_agent *agent, const struct snmp_table_row *row,
                                     uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_INTEGER;
    /* Default role unassigned(1) */
    vb->val.int_val = 1;
    return 0;
}

static int cb_table_sensor_col5_temp(struct snmp_agent *agent, const struct snmp_table_row *row,
                                     uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_INTEGER;

    raw_sensor_readings_t raw;
    memset(&raw, 0, sizeof(raw));
    if (app_facade_sensor_get_raw(&raw) == 0 && row->index >= 1U && row->index <= 16U)
    {
        uint8_t ch = (uint8_t)(row->index - 1U);
        if ((raw.cwt_valid_mask[0] & (1U << ch)) != 0U)
        {
            float t = raw.cwt_temperatures_c[0][ch];
            vb->val.int_val = (int32_t)(t * 10.0f + (t >= 0.0f ? 0.5f : -0.5f));
            return 0;
        }
    }

    vb->val.int_val = -9999;
    return 0;
}

static int cb_table_sensor_col6_status(struct snmp_agent *agent, const struct snmp_table_row *row,
                                       uint8_t column, struct snmp_varbind *vb)
{
    ARG_UNUSED(agent);
    ARG_UNUSED(column);
    vb->type = ASN1_TAG_INTEGER;

    raw_sensor_readings_t raw;
    memset(&raw, 0, sizeof(raw));
    if (app_facade_sensor_get_raw(&raw) == 0 && row->index >= 1U && row->index <= 16U)
    {
        uint8_t ch = (uint8_t)(row->index - 1U);
        if ((raw.cwt_open_probe_mask[0] & (1U << ch)) != 0U)
        {
            vb->val.int_val = 2; /* openProbe(2) */
            return 0;
        }
        if (!raw.cwt_comm_ok[0])
        {
            vb->val.int_val = 4; /* commFault(4) */
            return 0;
        }
        if ((raw.cwt_valid_mask[0] & (1U << ch)) != 0U)
        {
            vb->val.int_val = 1; /* normal(1) */
            return 0;
        }
    }

    vb->val.int_val = 5; /* disabled(5) */
    return 0;
}

/* =========================================================================
 * Table & Scalar Nodes Definitions
 * ========================================================================= */

static const uint8_t s_sensor_cols[] = {1, 2, 3, 4, 5, 6};
static const uint8_t s_sensor_types[] = {
    ASN1_TAG_INTEGER,
    ASN1_TAG_OCTET_STRING,
    ASN1_TAG_INTEGER,
    ASN1_TAG_INTEGER,
    ASN1_TAG_INTEGER,
    ASN1_TAG_INTEGER
};
static snmp_table_get_cb_t s_sensor_get_cbs[] = {
    cb_table_sensor_col1_id,
    cb_table_sensor_col2_name,
    cb_table_sensor_col3_phys_ch,
    cb_table_sensor_col4_role,
    cb_table_sensor_col5_temp,
    cb_table_sensor_col6_status
};

static struct snmp_table_row s_sensor_rows[16];

static struct snmp_mib_table s_sensor_table = {
    .table_oid = {.len = 10,
                  .ids = {SNMP_OID_BRIGHTCV3_BASE, 2, 1, 1}},
    .column_cnt = 6,
    .column_subids = s_sensor_cols,
    .column_types = s_sensor_types,
    .get_cbs = s_sensor_get_cbs,
    .set_cbs = NULL,
};

/* Scalar Nodes for Process Temperatures (1..16) */
#define NODE_PROCESS_TEMP(subid, tag) \
    { \
        .oid = {.len = 12, .ids = {SNMP_OID_BRIGHTCV3_BASE, 2, 2, subid, 0}}, \
        .type = tag, \
        .get_cb = cb_get_process_temp, \
        .set_cb = NULL, \
    }

/* Scalar Nodes for Energy Meter Telemetry (1..21) */
#define NODE_ENERGY(subid, tag) \
    { \
        .oid = {.len = 11, .ids = {SNMP_OID_BRIGHTCV3_BASE, 6, subid, 0}}, \
        .type = tag, \
        .get_cb = cb_get_energy_telemetry, \
        .set_cb = NULL, \
    }

static const struct snmp_mib_node s_telemetry_nodes[] = {
    /* Process Temperatures */
    NODE_PROCESS_TEMP(1,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(2,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(3,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(4,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(5,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(6,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(7,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(8,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(9,  ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(10, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(11, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(12, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(13, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(14, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(15, ASN1_TAG_INTEGER),
    NODE_PROCESS_TEMP(16, SNMP_TAG_GAUGE32),

    /* 3-Phase AC Energy Meter */
    NODE_ENERGY(1,  ASN1_TAG_INTEGER),
    NODE_ENERGY(2,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(3,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(4,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(5,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(6,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(7,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(8,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(9,  SNMP_TAG_GAUGE32),
    NODE_ENERGY(10, ASN1_TAG_INTEGER),
    NODE_ENERGY(11, ASN1_TAG_INTEGER),
    NODE_ENERGY(12, ASN1_TAG_INTEGER),
    NODE_ENERGY(13, ASN1_TAG_INTEGER),
    NODE_ENERGY(14, ASN1_TAG_INTEGER),
    NODE_ENERGY(15, ASN1_TAG_INTEGER),
    NODE_ENERGY(16, ASN1_TAG_INTEGER),
    NODE_ENERGY(17, ASN1_TAG_INTEGER),
    NODE_ENERGY(18, ASN1_TAG_INTEGER),
    NODE_ENERGY(19, ASN1_TAG_INTEGER),
    NODE_ENERGY(20, SNMP_TAG_GAUGE32),
    NODE_ENERGY(21, SNMP_TAG_GAUGE32),
};

int adapter_snmp_telemetry_init(struct snmp_agent *agent)
{
    if (agent == NULL)
    {
        return -EINVAL;
    }

    /* 1. Set Device Enterprise OID (1.3.6.1.4.1.3134.2.3) */
    struct snmp_oid ent_oid = {
        .len = 8,
        .ids = {SNMP_OID_BRIGHTCV3_BASE}
    };
    int ret = snmp_set_device_enterprise_oid(agent, &ent_oid);
    if (ret != 0)
    {
        LOG_WRN("failed to set enterprise OID: %d", ret);
    }

    /* 2. Register Scalar Telemetry Nodes (Process Temps + Energy Meter) */
    ret = snmp_mib_register_nodes(agent, s_telemetry_nodes, ARRAY_SIZE(s_telemetry_nodes));
    if (ret != 0)
    {
        LOG_ERR("failed to register telemetry nodes: %d", ret);
        return ret;
    }

    /* 3. Register Sensor Channel Table */
    ret = snmp_mib_register_table(agent, &s_sensor_table);
    if (ret != 0)
    {
        LOG_WRN("failed to register sensor table: %d", ret);
    }
    else
    {
        for (uint32_t i = 0; i < 16U; i++)
        {
            s_sensor_rows[i].index = i + 1U;
            s_sensor_rows[i].user_data = NULL;
            (void)snmp_mib_table_add_row(agent, &s_sensor_table, &s_sensor_rows[i]);
        }
    }

    LOG_INF("registered %u scalar telemetry nodes + 16-channel sensor table",
            (unsigned int)ARRAY_SIZE(s_telemetry_nodes));
    return 0;
}

#endif /* CONFIG_SNMP_AGENT */
