/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_bkpsram.c
 * @brief Implementation of in-memory Fake Backup SRAM adapter for host unit tests.
 */

#include "fake_bkpsram.h"

#include <errno.h>
#include <string.h>

static int fake_init_op(void *impl)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    if (self->simulated_error != 0) {
        return self->simulated_error;
    }
    return bkpsram_ring_init(&self->ring, self->storage, sizeof(self->storage));
}

static int fake_write_record_op(void *impl, alarm_nv_record_t *record)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL || record == NULL) {
        return -EINVAL;
    }
    if (self->simulated_error != 0) {
        return self->simulated_error;
    }
    return bkpsram_ring_push(&self->ring, record);
}

static int fake_get_next_uncommitted_op(void *impl,
                                        uint32_t cursor,
                                        alarm_nv_record_t *out_record,
                                        uint32_t *next_cursor)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    if (self->simulated_error != 0) {
        return self->simulated_error;
    }
    return bkpsram_ring_get_next_uncommitted(&self->ring, cursor, out_record, next_cursor);
}

static int fake_mark_committed_op(void *impl, uint32_t seq_id)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    if (self->simulated_error != 0) {
        return self->simulated_error;
    }
    return bkpsram_ring_mark_committed(&self->ring, seq_id);
}

static uint32_t fake_uncommitted_count_op(void *impl)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL) {
        return 0U;
    }
    return bkpsram_ring_uncommitted_count(&self->ring);
}

static bool fake_is_valid_op(void *impl)
{
    fake_bkpsram_t *self = (fake_bkpsram_t *)impl;
    if (self == NULL) {
        return false;
    }
    return bkpsram_ring_is_valid(&self->ring);
}

static const i_bkpsram_api_t s_fake_bkpsram_api = {
    .init = fake_init_op,
    .write_record = fake_write_record_op,
    .get_next_uncommitted = fake_get_next_uncommitted_op,
    .mark_committed = fake_mark_committed_op,
    .uncommitted_count = fake_uncommitted_count_op,
    .is_valid = fake_is_valid_op,
};

int fake_bkpsram_init(fake_bkpsram_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }
    memset(self->storage, 0, sizeof(self->storage));
    self->simulated_error = 0;
    self->is_initialized = true;
    return bkpsram_ring_init(&self->ring, self->storage, sizeof(self->storage));
}

void fake_bkpsram_reset(fake_bkpsram_t *self)
{
    if (self != NULL) {
        memset(self->storage, 0, sizeof(self->storage));
        (void)bkpsram_ring_format(&self->ring);
        self->simulated_error = 0;
    }
}

i_bkpsram_t fake_bkpsram_port(fake_bkpsram_t *self)
{
    i_bkpsram_t port = {
        .api = &s_fake_bkpsram_api,
        .impl = self,
    };
    return port;
}

static fake_bkpsram_t s_default_fake_bkpsram;

i_bkpsram_t fake_bkpsram_get_port(void)
{
    static bool init_done = false;
    if (!init_done) {
        (void)fake_bkpsram_init(&s_default_fake_bkpsram);
        init_done = true;
    }
    return fake_bkpsram_port(&s_default_fake_bkpsram);
}
