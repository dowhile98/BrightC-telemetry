/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_event_mediator.c
 * @brief Inter-Subsystem Event Mediator and Zbus Listener Hub Implementation.
 */

#include "infrastructure/di/app_event_mediator.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "infrastructure/di/composition_root.h"
#include "application/zbus/sensor_channels.h"
#include "application/zbus/digital_input_channels.h"
#include "application/zbus/hvac_channels.h"
#include "application/zbus/alarm_channels.h"
#include "application/zbus/indicator_channels.h"
#include "application/zbus/storage_channels.h"
#include "application/zbus/network_channels.h"
#include "application/zbus/system_channels.h"

#include "application/active_objects/ao_hvac.h"
#include "application/active_objects/ao_alarm_manager.h"
#include "application/active_objects/ao_system_indicator.h"
#include "application/active_objects/ao_datalogger.h"
#include "interfaces/i_bkpsram.h"

#include "infrastructure/adapters/adapter_alarm_detector_di.h"
#include "infrastructure/adapters/adapter_alarm_detector_temp.h"
#include "infrastructure/adapters/adapter_alarm_detector_hvac.h"

LOG_MODULE_REGISTER(app_event_mediator, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * Active container instance reference for routing Zbus notifications to subsystems.
 */
static app_container_t *s_active_container = NULL;

int app_event_mediator_bind(app_container_t *container)
{
    if (container == NULL) {
        return -EINVAL;
    }
    s_active_container = container;
    return 0;
}

void app_event_mediator_unbind(void)
{
    s_active_container = NULL;
}

app_container_t *app_event_mediator_get_container(void)
{
    return s_active_container;
}

/* =========================================================================
 * Zbus Observers & Listeners
 * ========================================================================= */

static void on_hvac_sensor_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_sensor_snapshot || s_active_container == NULL) {
        return;
    }

    const sensor_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->hvac.initialized) {
        (void)ao_hvac_feed_sensor_snapshot(&s_active_container->hvac.ao,
                                           msg->temperatures,
                                           msg->valid_mask,
                                           64U);
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_sensor_feed, on_hvac_sensor_feed_cb);

static void on_hvac_di_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_digital_input_snapshot || s_active_container == NULL) {
        return;
    }

    const di_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (!s_active_container->hvac.initialized) {
        return;
    }

    /* 1. Fire alarm safety interlock */
    bool fire_active = false;
    for (uint8_t i = 0U; i < msg->channel_count; i++) {
        if (msg->roles[i] == DI_ROLE_SMOKE_FIRE) {
            fire_active = msg->is_active[i];
            break;
        }
    }
    (void)ao_hvac_set_fire(&s_active_container->hvac.ao, fire_active);

    /* 2. Physical bypass switch interlock */
    bool bypass_sw_active = false;
    for (uint8_t i = 0U; i < msg->channel_count; i++) {
        if (msg->roles[i] == DI_ROLE_BYPASS_SWITCH) {
            bypass_sw_active = msg->is_active[i];
            break;
        }
    }
    for (uint8_t g = 0U; g < (uint8_t)HVAC_MAX_GROUPS; g++) {
        (void)ao_hvac_set_bypass_cause(&s_active_container->hvac.ao, g,
                                       BYPASS_CAUSE_PHYSICAL_SWITCH, bypass_sw_active);
    }

    /* 3. HVAC Unit Mechanical Alarms (Units 0..7) */
    for (uint8_t u = 0U; u < (uint8_t)HVAC_MAX_UNITS_TOTAL; u++) {
        di_role_t base_role = (di_role_t)(DI_ROLE_UNIT_0_ALARM_HP + (u * 4U));

        bool hp = di_snapshot_role_active_or_stale(msg, base_role);
        bool lp = di_snapshot_role_active_or_stale(msg, (di_role_t)(base_role + 1U));
        bool flow = di_snapshot_role_active_or_stale(msg, (di_role_t)(base_role + 2U));
        bool volt = di_snapshot_role_active_or_stale(msg, (di_role_t)(base_role + 3U));

        (void)ao_hvac_set_unit_alarms(&s_active_container->hvac.ao, u, hp, lp, flow, volt);
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_di_feed, on_hvac_di_feed_cb);

static void on_hvac_event_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_hvac_event) {
        return;
    }

    const hvac_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL) {
        return;
    }

    LOG_INF("HVAC Event: grp=%u unit=%u type=%d ts=%u",
            ev->group_id, ev->unit_id, (int)ev->type, ev->timestamp_ms);

    if (s_active_container != NULL && s_active_container->datalogger.initialized) {
        datalogger_event_envelope_t envelope;
        memset(&envelope, 0, sizeof(envelope));
        envelope.type = DATALOGGER_EVT_HVAC_EVENT;
        envelope.seq_id = datalogger_next_seq_id();
        envelope.uptime_ms = (uint64_t)k_uptime_get();
        envelope.payload.hvac = *ev;
        (void)ao_datalogger_post_event(&s_active_container->datalogger.ao, &envelope);
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_event_logger, on_hvac_event_cb);

static void on_alarm_di_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_digital_input_state || s_active_container == NULL) {
        return;
    }

    const di_transition_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL) {
        return;
    }

    if (s_active_container->alarm.initialized) {
        adapter_alarm_detector_di_process_transition(&s_active_container->alarm.det_di, ev);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_di_feed, on_alarm_di_feed_cb);

static void on_alarm_sensor_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_sensor_snapshot || s_active_container == NULL) {
        return;
    }

    const sensor_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->alarm.initialized) {
        adapter_alarm_detector_temp_process_snapshot(&s_active_container->alarm.det_temp, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_sensor_feed, on_alarm_sensor_feed_cb);

static void on_alarm_hvac_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_hvac_status || s_active_container == NULL) {
        return;
    }

    const hvac_status_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->alarm.initialized) {
        adapter_alarm_detector_hvac_process_status(&s_active_container->alarm.det_hvac, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_hvac_feed, on_alarm_hvac_feed_cb);

static void on_indicator_alarm_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_system_alarm || s_active_container == NULL) {
        return;
    }

    const alarm_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->indicators.initialized) {
        (void)ao_system_indicator_post_alarm(&s_active_container->indicators.ao, msg);
    }
}

ZBUS_LISTENER_DEFINE(lis_indicator_alarm_feed, on_indicator_alarm_feed_cb);

static void on_alarm_transition_logger_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_alarm_transition) {
        return;
    }

    const alarm_transition_event_t *ev = zbus_chan_const_msg(chan);
    if (ev == NULL) {
        return;
    }

    const char *tr_str = "UNKNOWN";
    switch (ev->event_type) {
    case ALARM_EVENT_ACTIVATED:      tr_str = "ACTIVATED"; break;
    case ALARM_EVENT_CLEARED:        tr_str = "CLEARED"; break;
    case ALARM_EVENT_ACKNOWLEDGED:   tr_str = "ACKNOWLEDGED"; break;
    case ALARM_EVENT_LATCHED:        tr_str = "LATCHED"; break;
    case ALARM_EVENT_SOURCE_CHANGED: tr_str = "SRC_CHANGED"; break;
    default: break;
    }

    LOG_INF("ALARM [0x%04X] %s: state=%d sev=%d val=%.2f mask=0x%08X ts=%llu ms",
            ev->alarm_id, tr_str, (int)ev->new_state, (int)ev->severity,
            (double)ev->trigger_value, (unsigned int)ev->contributing_sources_mask,
            (unsigned long long)ev->timestamp_ms);

    if (s_active_container != NULL && s_active_container->datalogger.initialized) {
        uint32_t seq = datalogger_next_seq_id();
        uint64_t uptime = (uint64_t)k_uptime_get();

        /* Mirror critical/fatal alarms to non-volatile Backup SRAM immediately */
        if (ev->severity >= ALARM_SEVERITY_CRITICAL &&
            i_bkpsram_is_bound(&s_active_container->datalogger.bkp_port)) {
            alarm_nv_record_t rec;
            memset(&rec, 0, sizeof(rec));
            rec.seq_id = seq;
            rec.uptime_ms = uptime;
            rec.epoch_s = (uint32_t)(ev->timestamp_ms / 1000ULL);
            rec.alarm_id = ev->alarm_id;
            rec.source_subsystem = (uint8_t)ev->source_category;
            rec.transition_type = (uint8_t)ev->event_type;
            rec.threshold_value = 0.0f;
            rec.measured_value = ev->trigger_value;
            rec.severity = (uint8_t)ev->severity;
            rec.status_flags = ALARM_NV_FLAG_NONE;
            snprintf(rec.tag, sizeof(rec.tag), "ALM_%04X", ev->alarm_id);

            (void)i_bkpsram_write_record(&s_active_container->datalogger.bkp_port, &rec);
        }

        /* Forward to datalogger queue for batched SD card streaming */
        datalogger_event_envelope_t envelope;
        memset(&envelope, 0, sizeof(envelope));
        envelope.type = DATALOGGER_EVT_ALARM_TRANSITION;
        envelope.seq_id = seq;
        envelope.uptime_ms = uptime;
        envelope.payload.alarm = *ev;
        (void)ao_datalogger_post_event(&s_active_container->datalogger.ao, &envelope);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_logger, on_alarm_transition_logger_cb);

static void on_storage_sd_status_logger_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_storage_sd_status || s_active_container == NULL) {
        return;
    }

    const sd_card_status_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->datalogger.initialized) {
        datalogger_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.type = DATALOGGER_EVT_STORAGE_STATUS;
        env.seq_id = datalogger_next_seq_id();
        env.uptime_ms = (uint64_t)k_uptime_get();
        env.payload.storage_status = msg->status;
        (void)ao_datalogger_post_event(&s_active_container->datalogger.ao, &env);
    }
}

ZBUS_LISTENER_DEFINE(lis_storage_sd_status_logger, on_storage_sd_status_logger_cb);

static void on_system_event_logger_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_system_event || s_active_container == NULL) {
        return;
    }

    const system_event_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->datalogger.initialized) {
        datalogger_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.type = DATALOGGER_EVT_SYSTEM_EVENT;
        env.seq_id = datalogger_next_seq_id();
        env.uptime_ms = (uint64_t)k_uptime_get();
        env.payload.system = *msg;
        (void)ao_datalogger_post_event(&s_active_container->datalogger.ao, &env);
    }
}

ZBUS_LISTENER_DEFINE(lis_system_event_logger, on_system_event_logger_cb);

static void on_alarm_net_eth_feed_cb(const struct zbus_channel *chan)
{
    if (chan != &chan_network_eth_state || s_active_container == NULL) {
        return;
    }

    const net_state_msg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    if (s_active_container->alarm.initialized) {
        bool is_down = (msg->status.state != NET_IF_STATE_CONNECTED);
        (void)ao_alarm_manager_post_condition(&s_active_container->alarm.ao,
                                              ALARM_ID_INFRA_ETHERNET_DOWN,
                                              is_down, 0.0f);
    }
}

ZBUS_LISTENER_DEFINE(lis_alarm_net_eth_feed, on_alarm_net_eth_feed_cb);
