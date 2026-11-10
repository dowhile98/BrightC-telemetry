/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_aggregate.c
 * @brief Implementation of the Pure Domain Multi-Source Aggregated Alarm.
 */

#include "domain/alarm/alarm_aggregate.h"

void alarm_aggregate_init(alarm_aggregate_entity_t *self,
                          uint16_t id,
                          alarm_source_category_t source,
                          alarm_severity_t severity,
                          alarm_priority_t priority,
                          uint32_t assigned_sources_mask,
                          bool is_latch_enabled)
{
    if (self == NULL) {
        return;
    }

    self->id = id;
    self->source_category = source;
    self->severity = severity;
    self->priority = priority;
    self->current_state = ALARM_STATE_INACTIVE;

    self->assigned_sources_mask = assigned_sources_mask;
    self->contributing_sources_mask = 0U;

    self->is_enabled = true;
    self->is_latch_enabled = is_latch_enabled;

    self->triggered_timestamp_ms = 0;
    self->cleared_timestamp_ms = 0;
    self->last_trigger_value = 0.0f;
    self->trigger_count = 0;
}

bool alarm_aggregate_update_source(alarm_aggregate_entity_t *self,
                                   uint8_t source_index,
                                   bool is_asserted,
                                   float trigger_val,
                                   uint64_t timestamp_ms,
                                   alarm_transition_event_t *out_event)
{
    if (self == NULL || !self->is_enabled || out_event == NULL || source_index >= 32U) {
        return false;
    }

    uint32_t bit = (1U << source_index);
    if ((self->assigned_sources_mask & bit) == 0U) {
        /* Source is not part of this aggregate group */
        return false;
    }

    alarm_state_t prev = self->current_state;
    bool was_contributing = ((self->contributing_sources_mask & bit) != 0U);

    if (is_asserted) {
        self->contributing_sources_mask |= bit;

        /* First source enters alarm -> Rising Edge (ON_PRESS) */
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
            out_event->contributing_sources_mask = self->contributing_sources_mask;
            out_event->trigger_value = trigger_val;
            out_event->timestamp_ms = timestamp_ms;

            return true;
        }

        /* Source changed while already active */
        if (!was_contributing) {
            out_event->alarm_id = self->id;
            out_event->event_type = ALARM_EVENT_SOURCE_CHANGED;
            out_event->previous_state = prev;
            out_event->new_state = self->current_state;
            out_event->severity = self->severity;
            out_event->priority = self->priority;
            out_event->source_category = self->source_category;
            out_event->contributing_sources_mask = self->contributing_sources_mask;
            out_event->trigger_value = trigger_val;
            out_event->timestamp_ms = timestamp_ms;

            return true;
        }
    } else {
        self->contributing_sources_mask &= ~bit;

        /* If this source was not contributing, nothing changed */
        if (!was_contributing) {
            return false;
        }

        /* Check if ALL assigned sources have now cleared */
        if (self->contributing_sources_mask == 0U) {
            if (self->current_state == ALARM_STATE_ACTIVE) {
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
                out_event->contributing_sources_mask = 0U;
                out_event->trigger_value = trigger_val;
                out_event->timestamp_ms = timestamp_ms;

                return true;
            }
        } else {
            /* Still other sources contributing -> Remains ACTIVE! */
            out_event->alarm_id = self->id;
            out_event->event_type = ALARM_EVENT_SOURCE_CHANGED;
            out_event->previous_state = prev;
            out_event->new_state = self->current_state;
            out_event->severity = self->severity;
            out_event->priority = self->priority;
            out_event->source_category = self->source_category;
            out_event->contributing_sources_mask = self->contributing_sources_mask;
            out_event->trigger_value = trigger_val;
            out_event->timestamp_ms = timestamp_ms;

            return true;
        }
    }

    return false;
}

bool alarm_aggregate_reset_latch(alarm_aggregate_entity_t *self,
                                 uint64_t timestamp_ms,
                                 alarm_transition_event_t *out_event)
{
    if (self == NULL || !self->is_enabled || out_event == NULL) {
        return false;
    }

    if (self->current_state == ALARM_STATE_LATCHED && self->contributing_sources_mask == 0U) {
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
        out_event->contributing_sources_mask = 0U;
        out_event->trigger_value = 0.0f;
        out_event->timestamp_ms = timestamp_ms;

        return true;
    }

    return false;
}

size_t alarm_aggregate_get_active_source_count(const alarm_aggregate_entity_t *self)
{
    if (self == NULL) {
        return 0;
    }
    size_t count = 0;
    uint32_t mask = self->contributing_sources_mask;
    while (mask != 0U) {
        count += (mask & 1U);
        mask >>= 1U;
    }
    return count;
}
