/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake implementation of i_sensor_matrix port.
 */

#include "fake_sensor_matrix.h"
#include "domain/services/sensor_matrix_mapper.h"
#include <string.h>

static fake_sensor_matrix_state_t s_fake_state;

static int fake_get_frame(void *impl, thermal_matrix_frame_t *out_frame)
{
    fake_sensor_matrix_state_t *s = (fake_sensor_matrix_state_t *)impl;
    s->get_frame_calls++;
    if (s->get_frame_ret != 0) {
        return s->get_frame_ret;
    }
    memcpy(out_frame, &s->frame, sizeof(thermal_matrix_frame_t));
    return 0;
}

static int fake_get_raw_readings(void *impl, raw_sensor_readings_t *out_raw)
{
    fake_sensor_matrix_state_t *s = (fake_sensor_matrix_state_t *)impl;
    s->get_raw_calls++;
    if (s->get_raw_ret != 0) {
        return s->get_raw_ret;
    }
    memcpy(out_raw, &s->raw, sizeof(raw_sensor_readings_t));
    return 0;
}

static int fake_set_channel_config(void *impl, physical_channel_id_t ch, const sensor_channel_cfg_t *cfg)
{
    fake_sensor_matrix_state_t *s = (fake_sensor_matrix_state_t *)impl;
    s->set_cfg_calls++;
    if (s->set_cfg_ret != 0) {
        return s->set_cfg_ret;
    }
    s->configs[ch] = *cfg;
    s->channel_configured[ch] = true;
    return 0;
}

static int fake_get_channel_config(void *impl, physical_channel_id_t ch, sensor_channel_cfg_t *out_cfg)
{
    fake_sensor_matrix_state_t *s = (fake_sensor_matrix_state_t *)impl;
    s->get_cfg_calls++;
    if (s->get_cfg_ret != 0) {
        return s->get_cfg_ret;
    }
    *out_cfg = s->configs[ch];
    return 0;
}

static int fake_update(void *impl)
{
    fake_sensor_matrix_state_t *s = (fake_sensor_matrix_state_t *)impl;
    s->update_calls++;
    if (s->update_ret != 0) {
        return s->update_ret;
    }
    if (s->auto_map_on_update) {
        return sensor_matrix_map(&s->raw, s->configs, PHYS_CH_MAX, &s->frame);
    }
    return 0;
}

static const i_sensor_matrix_api_t s_fake_api = {
    .get_frame = fake_get_frame,
    .get_raw_readings = fake_get_raw_readings,
    .set_channel_config = fake_set_channel_config,
    .get_channel_config = fake_get_channel_config,
    .update = fake_update,
};

void fake_sensor_matrix_reset(void)
{
    memset(&s_fake_state, 0, sizeof(s_fake_state));
    sensor_matrix_init_default_pilot_config(s_fake_state.configs, PHYS_CH_MAX);
    for (size_t i = 0; i < PHYS_CH_MAX; i++) {
        s_fake_state.channel_configured[i] = true;
    }
}

i_sensor_matrix_t fake_sensor_matrix_get_port(void)
{
    i_sensor_matrix_t port = {
        .api = &s_fake_api,
        .impl = &s_fake_state,
    };
    return port;
}

void fake_sensor_matrix_set_frame(const thermal_matrix_frame_t *frame)
{
    if (frame != NULL) {
        memcpy(&s_fake_state.frame, frame, sizeof(thermal_matrix_frame_t));
    }
}

void fake_sensor_matrix_set_raw_readings(const raw_sensor_readings_t *raw)
{
    if (raw != NULL) {
        memcpy(&s_fake_state.raw, raw, sizeof(raw_sensor_readings_t));
    }
}

void fake_sensor_matrix_set_auto_map(bool enable)
{
    s_fake_state.auto_map_on_update = enable;
}

const fake_sensor_matrix_state_t *fake_sensor_matrix_get_state(void)
{
    return &s_fake_state;
}
