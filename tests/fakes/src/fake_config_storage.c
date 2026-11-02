/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake config storage port implementation.
 */

#include "fake_config_storage.h"

#include <errno.h>
#include <string.h>

static fake_config_storage_state_t s_fake_storage;

static int fake_storage_init(void *impl)
{
    fake_config_storage_state_t *s = (fake_config_storage_state_t *)impl;
    s->init_calls++;
    if (s->simulated_init_error != 0) {
        return s->simulated_init_error;
    }
    return 0;
}

static int fake_storage_save(void *impl, config_domain_t domain, const void *data, size_t size)
{
    fake_config_storage_state_t *s = (fake_config_storage_state_t *)impl;
    s->save_calls++;

    if (s->simulated_save_error != 0) {
        return s->simulated_save_error;
    }

    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    if (size > FAKE_CONFIG_STORAGE_MAX_SLOT_SIZE) {
        return -EFBIG;
    }

    fake_config_domain_slot_t *slot = &s->slots[domain];
    memcpy(slot->data, data, size);
    slot->size = size;
    slot->has_data = true;
    slot->save_count++;

    return 0;
}

static int fake_storage_load(void *impl, config_domain_t domain, void *out_data, size_t max_size, size_t *out_size)
{
    fake_config_storage_state_t *s = (fake_config_storage_state_t *)impl;
    s->load_calls++;

    if (s->simulated_load_error != 0) {
        return s->simulated_load_error;
    }

    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    fake_config_domain_slot_t *slot = &s->slots[domain];
    slot->load_count++;

    if (!slot->has_data) {
        return -ENOENT;
    }

    if (max_size < slot->size) {
        return -ENOSPC;
    }

    memcpy(out_data, slot->data, slot->size);
    if (out_size != NULL) {
        *out_size = slot->size;
    }

    return 0;
}

static int fake_storage_reset_defaults(void *impl, config_domain_t domain)
{
    fake_config_storage_state_t *s = (fake_config_storage_state_t *)impl;
    s->reset_defaults_calls++;

    if (s->simulated_reset_error != 0) {
        return s->simulated_reset_error;
    }

    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    fake_config_domain_slot_t *slot = &s->slots[domain];
    slot->reset_count++;
    slot->has_data = false;
    slot->size = 0;
    memset(slot->data, 0, sizeof(slot->data));

    return 0;
}

static const i_config_storage_api_t s_fake_storage_api = {
    .init = fake_storage_init,
    .save = fake_storage_save,
    .load = fake_storage_load,
    .reset_defaults = fake_storage_reset_defaults,
};

void fake_config_storage_reset(void)
{
    memset(&s_fake_storage, 0, sizeof(s_fake_storage));
}

i_config_storage_t fake_config_storage_get_port(void)
{
    i_config_storage_t port = {
        .api = &s_fake_storage_api,
        .impl = &s_fake_storage,
    };
    return port;
}

const fake_config_storage_state_t *fake_config_storage_get_state(void)
{
    return &s_fake_storage;
}

void fake_config_storage_set_errors(int init_err, int save_err, int load_err, int reset_err)
{
    s_fake_storage.simulated_init_error = init_err;
    s_fake_storage.simulated_save_error = save_err;
    s_fake_storage.simulated_load_error = load_err;
    s_fake_storage.simulated_reset_error = reset_err;
}

int fake_config_storage_seed_domain(config_domain_t domain, const void *data, size_t size)
{
    if (!config_domain_is_valid(domain) || data == NULL || size == 0) {
        return -EINVAL;
    }
    if (size > FAKE_CONFIG_STORAGE_MAX_SLOT_SIZE) {
        return -EFBIG;
    }

    fake_config_domain_slot_t *slot = &s_fake_storage.slots[domain];
    memcpy(slot->data, data, size);
    slot->size = size;
    slot->has_data = true;
    return 0;
}
