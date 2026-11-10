/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_entity.c
 * @brief Implementation of the Pure Domain Alarm State Machine.
 */

#include "domain/alarm/alarm_entity.h"

void alarm_entity_init(alarm_entity_t *self,
                       uint16_t id,
                       alarm_source_category_t source,
                       alarm_severity_t severity,
                       alarm_priority_t priority,
                       bool is_latch_enabled,
                       bool is_ack_supported)
{
    if (self == NULL) {
        return;
    }

    self->id = id;
    self->source_category = source;
    self->severity = severity;
    self->priority = priority;
    self->current_state = ALARM_STATE_INACTIVE;

    self->is_enabled = true;
    self->is_latch_enabled = is_latch_enabled;
    self->is_ack_supported = is_ack_supported;

    self->triggered_timestamp_ms = 0;
    self->cleared_timestamp_ms = 0;
    self->last_trigger_value = 0.0f;
    self->trigger_count = 0;
}

void alarm_entity_set_enabled(alarm_entity_t *self, bool enabled)
{
    if (self != NULL) {
        self->is_enabled = enabled;
    }
}

bool alarm_entity_update_condition(alarm_entity_t *self,
                                   bool is_asserted,
                                   float trigger_val,
                                   uint64_t timestamp_ms,
                                   alarm_transition_event_t *out_event)
{
    if (self == NULL || !self->is_enabled || out_event == NULL) {
        return false;
    }

    alarm_state_t prev = self->current_state;

    if (is_asserted) {
        /* Rising Edge: INACTIVE -> ACTIVE (ON_PRESS) */
        if (self->current_state == ALARM_STATE_INACTIVE) {
            self->current_state = ALARM_STATE_ACTIVE;
            self->triggered_timestamp_ms = timestamp_ms;
            self->last_trigger_value = trigger_val;
            self->trigger_count++;

            out_event->alarm_id = self->id;
            out_event->event_type = ALARM_EVENT_ACTIVATED;
            out_event->previous_state = prev;
            out_event->new_state = self->current_state;
            out_event->severity = self->severity;
            out_event->priority = self->priority;
            out_event->source_category = self->source_category;
            out_event->contributing_sources_mask = 0;
            out_event->trigger_value = trigger_val;
            out_event->timestamp_ms = timestamp_ms;

            return true; /* Transition occurred */
        }
    } else {
        /* Falling Edge: ACTIVE / ACKNOWLEDGED -> INACTIVE / LATCHED (ON_RELEASE) */
        if (self->current_state == ALARM_STATE_ACTIVE ||
            self->current_state == ALARM_STATE_ACKNOWLEDGED) {

            if (self->is_latch_enabled) {
                self->current_state = ALARM_STATE_LATCHED;
                out_event->event_type = ALARM_EVENT_LATCHED;
            } else {
                self->current_state = ALARM_STATE_INACTIVE;
                self->cleared_timestamp_ms = timestamp_ms;
                out_event->event_type = ALARM_EVENT_CLEARED;
            }

            out_event->alarm_id = self->id;
            out_event->previous_state = prev;
            out_event->new_state = self->current_state;
            out_event->severity = self->severity;
            out_event->priority = self->priority;
            out_event->source_category = self->source_category;
            out_event->contributing_sources_mask = 0;
            out_event->trigger_value = trigger_val;
            out_event->timestamp_ms = timestamp_ms;

            return true; /* Transition occurred */
        }
    }

    return false; /* No transition: suppress duplicates */
}

bool alarm_entity_acknowledge(alarm_entity_t *self,
                              uint64_t timestamp_ms,
                              alarm_transition_event_t *out_event)
{
    if (self == NULL || !self->is_enabled || !self->is_ack_supported || out_event == NULL) {
        return false;
    }

    if (self->current_state == ALARM_STATE_ACTIVE) {
        alarm_state_t prev = self->current_state;
        self->current_state = ALARM_STATE_ACKNOWLEDGED;

        out_event->alarm_id = self->id;
        out_event->event_type = ALARM_EVENT_ACKNOWLEDGED;
        out_event->previous_state = prev;
        out_event->new_state = self->current_state;
        out_event->severity = self->severity;
        out_event->priority = self->priority;
        out_event->source_category = self->source_category;
        out_event->contributing_sources_mask = 0;
        out_event->trigger_value = self->last_trigger_value;
        out_event->timestamp_ms = timestamp_ms;

        return true;
    }

    return false;
}

bool alarm_entity_reset_latch(alarm_entity_t *self,
                              uint64_t timestamp_ms,
                              alarm_transition_event_t *out_event)
{
    if (self == NULL || !self->is_enabled || out_event == NULL) {
        return false;
    }

    if (self->current_state == ALARM_STATE_LATCHED) {
        alarm_state_t prev = self->current_state;
        self->current_state = ALARM_STATE_INACTIVE;
        self->cleared_timestamp_ms = timestamp_ms;

        out_event->alarm_id = self->id;
        out_event->event_type = ALARM_EVENT_CLEARED;
        out_event->previous_state = prev;
        out_event->new_state = self->current_state;
        out_event->severity = self->severity;
        out_event->priority = self->priority;
        out_event->source_category = self->source_category;
        out_event->contributing_sources_mask = 0;
        out_event->trigger_value = 0.0f;
        out_event->timestamp_ms = timestamp_ms;

        return true;
    }

    return false;
}
