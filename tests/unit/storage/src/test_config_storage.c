/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "common/config_types.h"
#include "common/config_envelope.h"
#include "interfaces/i_config_storage.h"
#include "fake_config_storage.h"
#include "application/zbus/config_channels.h"
#include "application/active_objects/ao_storage.h"

ZTEST_SUITE(storage_suite, NULL, NULL, NULL, NULL, NULL);




ZTEST(storage_suite, test_config_domain_enum_and_validation)
{
    zassert_equal(CONFIG_DOMAIN_DEVICE, 0, "CONFIG_DOMAIN_DEVICE should be 0");
    zassert_equal(CONFIG_DOMAIN_NET_ETH, 1, "CONFIG_DOMAIN_NET_ETH should be 1");
    zassert_equal(CONFIG_DOMAIN_NET_WIFI, 2, "CONFIG_DOMAIN_NET_WIFI should be 2");
    zassert_equal(CONFIG_DOMAIN_HVAC, 3, "CONFIG_DOMAIN_HVAC should be 3");
    zassert_equal(CONFIG_DOMAIN_CALIBRATION, 4, "CONFIG_DOMAIN_CALIBRATION should be 4");
    zassert_equal(CONFIG_DOMAIN_MAX, 5, "CONFIG_DOMAIN_MAX should be 5");

    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_DEVICE), "DEVICE must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_NET_ETH), "NET_ETH must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_NET_WIFI), "NET_WIFI must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_HVAC), "HVAC must be valid");
    zassert_true(config_domain_is_valid(CONFIG_DOMAIN_CALIBRATION), "CALIBRATION must be valid");

    zassert_false(config_domain_is_valid(CONFIG_DOMAIN_MAX), "MAX must be invalid");
    zassert_false(config_domain_is_valid((config_domain_t)-1), "-1 must be invalid");
    zassert_false(config_domain_is_valid((config_domain_t)99), "99 must be invalid");
}

ZTEST(storage_suite, test_config_event_type_validation)
{
    zassert_equal(CONFIG_EVENT_DIRTY, 0, "CONFIG_EVENT_DIRTY should be 0");
    zassert_equal(CONFIG_EVENT_PERSISTED, 1, "CONFIG_EVENT_PERSISTED should be 1");
    zassert_equal(CONFIG_EVENT_ERROR, 2, "CONFIG_EVENT_ERROR should be 2");

    zassert_true(config_event_type_is_valid(CONFIG_EVENT_DIRTY), "DIRTY must be valid");
    zassert_true(config_event_type_is_valid(CONFIG_EVENT_PERSISTED), "PERSISTED must be valid");
    zassert_true(config_event_type_is_valid(CONFIG_EVENT_ERROR), "ERROR must be valid");
    zassert_false(config_event_type_is_valid((config_event_type_t)3), "3 must be invalid");
    zassert_false(config_event_type_is_valid((config_event_type_t)-1), "-1 must be invalid");
}

ZTEST(storage_suite, test_config_domain_layout)
{
    size_t base = 0;
    size_t total = 0;
    size_t slot = 0;

    /* DEVICE: 0x0000, 256, 128 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_DEVICE, &base, &total, &slot), 0, "DEVICE layout error");
    zassert_equal(base, 0x0000, "DEVICE base");
    zassert_equal(total, 256, "DEVICE total");
    zassert_equal(slot, 128, "DEVICE slot");

    /* NET_ETH: 0x0100, 128, 64 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_NET_ETH, &base, &total, &slot), 0, "NET_ETH layout error");
    zassert_equal(base, 0x0100, "NET_ETH base");
    zassert_equal(total, 128, "NET_ETH total");
    zassert_equal(slot, 64, "NET_ETH slot");

    /* NET_WIFI: 0x0180, 640, 320 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_NET_WIFI, &base, &total, &slot), 0, "NET_WIFI layout error");
    zassert_equal(base, 0x0180, "NET_WIFI base");
    zassert_equal(total, 640, "NET_WIFI total");
    zassert_equal(slot, 320, "NET_WIFI slot");

    /* HVAC: 0x0400, 1152, 576 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_HVAC, &base, &total, &slot), 0, "HVAC layout error");
    zassert_equal(base, 0x0400, "HVAC base");
    zassert_equal(total, 1152, "HVAC total");
    zassert_equal(slot, 576, "HVAC slot");

    /* CALIBRATION: 0x0880, 384, 192 */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_CALIBRATION, &base, &total, &slot), 0, "CALIBRATION layout error");
    zassert_equal(base, 0x0880, "CALIBRATION base");
    zassert_equal(total, 384, "CALIBRATION total");
    zassert_equal(slot, 192, "CALIBRATION slot");

    /* Invalid domain */
    zassert_equal(config_domain_get_layout(CONFIG_DOMAIN_MAX, &base, &total, &slot), -EINVAL, "MAX invalid");
    zassert_equal(config_domain_get_layout((config_domain_t)99, &base, &total, &slot), -EINVAL, "99 invalid");
}

ZTEST(storage_suite, test_i_config_storage_wrappers_unbound_and_null)
{
    uint8_t buf[32];
    size_t out_sz = 0;

    zassert_false(i_config_storage_is_bound(NULL), "NULL storage handle is not bound");

    i_config_storage_t empty = {0};
    zassert_false(i_config_storage_is_bound(&empty), "Empty storage handle is not bound");

    /* Calls with NULL */
    zassert_equal(i_config_storage_init(NULL), -EINVAL, "init(NULL)");
    zassert_equal(i_config_storage_save(NULL, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -EINVAL, "save(NULL)");
    zassert_equal(i_config_storage_load(NULL, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EINVAL, "load(NULL)");
    zassert_equal(i_config_storage_reset_defaults(NULL, CONFIG_DOMAIN_DEVICE), -EINVAL, "reset_defaults(NULL)");

    /* Calls with unbound struct */
    zassert_equal(i_config_storage_init(&empty), -EINVAL, "init(empty)");
    zassert_equal(i_config_storage_save(&empty, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -EINVAL, "save(empty)");
    zassert_equal(i_config_storage_load(&empty, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EINVAL, "load(empty)");
    zassert_equal(i_config_storage_reset_defaults(&empty, CONFIG_DOMAIN_DEVICE), -EINVAL, "reset_defaults(empty)");
}

ZTEST(storage_suite, test_fake_config_storage_operations)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_true(i_config_storage_is_bound(&port), "Fake storage must be bound");

    /* 1. Init */
    zassert_equal(i_config_storage_init(&port), 0, "Init failed");
    const fake_config_storage_state_t *state = fake_config_storage_get_state();
    zassert_equal(state->init_calls, 1, "Init call counter mismatch");

    /* 2. Load non-existent data */
    uint8_t read_buf[64] = {0};
    size_t out_size = 0;
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), -ENOENT, "Should return -ENOENT for empty slot");

    /* 3. Save data */
    uint8_t dummy_payload[] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22};
    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_NET_ETH, dummy_payload, sizeof(dummy_payload)), 0, "Save failed");
    zassert_equal(state->save_calls, 1, "Save call counter mismatch");
    zassert_equal(state->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Slot save count mismatch");

    /* 4. Load saved data */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), 0, "Load failed");
    zassert_equal(out_size, sizeof(dummy_payload), "Size mismatch");
    zassert_mem_equal(read_buf, dummy_payload, sizeof(dummy_payload), "Content mismatch");

    /* Buffer too small */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, 2, &out_size), -ENOSPC, "Should return -ENOSPC");

    /* 5. Domain isolation check: another domain is still empty */
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_WIFI, read_buf, sizeof(read_buf), &out_size), -ENOENT, "WIFI should still be empty");

    /* 6. Reset defaults */
    zassert_equal(i_config_storage_reset_defaults(&port, CONFIG_DOMAIN_NET_ETH), 0, "Reset failed");
    zassert_equal(state->reset_defaults_calls, 1, "Reset call counter mismatch");
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_NET_ETH, read_buf, sizeof(read_buf), &out_size), -ENOENT, "Should be empty after reset");
}

ZTEST(storage_suite, test_fake_config_storage_error_simulation)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();

    fake_config_storage_set_errors(-EIO, -ENOMEM, -EBUSY, -EFAULT);
    uint8_t buf[16] = {0};
    size_t out_sz = 0;

    zassert_equal(i_config_storage_init(&port), -EIO, "Expected init error");
    zassert_equal(i_config_storage_save(&port, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf)), -ENOMEM, "Expected save error");
    zassert_equal(i_config_storage_load(&port, CONFIG_DOMAIN_DEVICE, buf, sizeof(buf), &out_sz), -EBUSY, "Expected load error");
    zassert_equal(i_config_storage_reset_defaults(&port, CONFIG_DOMAIN_DEVICE), -EFAULT, "Expected reset error");

    /* Clear errors */
    fake_config_storage_set_errors(0, 0, 0, 0);
    zassert_equal(i_config_storage_init(&port), 0, "Init should now succeed");
}

/* File-scope stack and queue for ao_storage tests */
K_THREAD_STACK_DEFINE(s_test_storage_stack, AO_STORAGE_STACK_SIZE);
K_MSGQ_DEFINE(s_test_storage_queue, sizeof(ao_storage_event_t), AO_STORAGE_QUEUE_DEPTH, 4);
static ao_storage_t s_test_ao_storage;

/* Dummy test provider */
static uint8_t s_mock_net_eth_data[16] = {1, 2, 3, 4, 5, 6, 7, 8};
static size_t s_mock_net_eth_size = 8;
static int s_mock_provider_err = 0;

static int test_net_eth_provider(void *ctx, config_domain_t domain, void *out_buf, size_t max_size, size_t *out_size)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(domain);
    if (s_mock_provider_err != 0) {
        return s_mock_provider_err;
    }
    if (max_size < s_mock_net_eth_size) {
        return -ENOSPC;
    }
    memcpy(out_buf, s_mock_net_eth_data, s_mock_net_eth_size);
    if (out_size != NULL) {
        *out_size = s_mock_net_eth_size;
    }
    return 0;
}

ZTEST(storage_suite, test_ao_storage_init_validation)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();

    /* Invalid arguments */
    zassert_equal(ao_storage_init(NULL, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL self");
    zassert_equal(ao_storage_init(&s_test_ao_storage, NULL, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL storage");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, NULL, s_test_storage_stack, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL queue");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, NULL, AO_STORAGE_STACK_SIZE), -EINVAL, "NULL stack");
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, 0), -EINVAL, "0 stack size");

    i_config_storage_t unbound = {0};
    zassert_equal(ao_storage_init(&s_test_ao_storage, &unbound, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Unbound storage initializes in degraded mode");
    zassert_true(ao_storage_is_degraded(&s_test_ao_storage), "Storage AO should be degraded");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit failed");

    /* Valid initialization with bound port */
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Valid init failed");
    zassert_false(ao_storage_is_degraded(&s_test_ao_storage), "Storage AO should not be degraded");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit failed");
}

ZTEST(storage_suite, test_ao_storage_provider_registration)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    /* Invalid provider registration */
    zassert_equal(ao_storage_register_provider(NULL, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), -EINVAL, "NULL self");
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_MAX, test_net_eth_provider, NULL), -EINVAL, "Invalid domain");
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, NULL, NULL), -EINVAL, "NULL fn");

    /* Valid registration */
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Valid registration");
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_ao_storage_sync_flush_and_error_handling)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    s_mock_provider_err = 0;
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Register");

    /* Mark domain dirty without running thread */
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    int flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 1, "Should have flushed 1 domain");
    zassert_equal(s_test_ao_storage.dirty_mask, 0, "Dirty mask should be cleared");

    const fake_config_storage_state_t *st = fake_config_storage_get_state();
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Save count should be 1");
    zassert_mem_equal(st->slots[CONFIG_DOMAIN_NET_ETH].data, s_mock_net_eth_data, s_mock_net_eth_size, "Saved data mismatch");

    /* Test error in provider */
    s_mock_provider_err = -EIO;
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 0, "Should have failed to persist");
    zassert_equal(s_test_ao_storage.dirty_mask, 0, "Dirty mask should still be cleared on handled error");

    /* Test error in storage save */
    s_mock_provider_err = 0;
    fake_config_storage_set_errors(0, -EIO, 0, 0);
    s_test_ao_storage.dirty_mask |= (1U << CONFIG_DOMAIN_NET_ETH);
    flushed = ao_storage_sync_flush(&s_test_ao_storage);
    zassert_equal(flushed, 0, "Should have failed to persist on storage error");
    fake_config_storage_set_errors(0, 0, 0, 0);

    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_ao_storage_lifecycle_and_debounce)
{
    fake_config_storage_reset();
    i_config_storage_t port = fake_config_storage_get_port();
    zassert_equal(ao_storage_init(&s_test_ao_storage, &port, &s_test_storage_queue, s_test_storage_stack, AO_STORAGE_STACK_SIZE), 0, "Init");

    s_mock_provider_err = 0;
    zassert_equal(ao_storage_register_provider(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, test_net_eth_provider, NULL), 0, "Register");

    /* Start thread */
    zassert_equal(ao_storage_start(&s_test_ao_storage), 0, "Start");
    zassert_equal(ao_storage_start(&s_test_ao_storage), -EALREADY, "Double start should return -EALREADY");

    /* Mark dirty */
    zassert_equal(ao_storage_mark_dirty(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, 0), 0, "Mark dirty");

    /* Check immediately: should NOT be written yet due to 500 ms debounce */
    const fake_config_storage_state_t *st = fake_config_storage_get_state();
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 0, "Should not be persisted immediately (debounce)");

    /* Immediate flush command */
    zassert_equal(ao_storage_flush_now(&s_test_ao_storage), 0, "Flush now");
    k_sleep(K_MSEC(50));
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 1, "Should be persisted after flush_now");

    /* Mark dirty again and let debounce timer expire */
    zassert_equal(ao_storage_mark_dirty(&s_test_ao_storage, CONFIG_DOMAIN_NET_ETH, 0), 0, "Mark dirty again");
    /* Wait for debounce (500 ms + margin) */
    k_sleep(K_MSEC(600));
    zassert_equal(st->slots[CONFIG_DOMAIN_NET_ETH].save_count, 2, "Should be persisted after debounce window");

    /* Stop Active Object */
    zassert_equal(ao_storage_stop(&s_test_ao_storage), 0, "Stop");
    zassert_equal(ao_storage_wait_stopped(&s_test_ao_storage, K_MSEC(2000)), 0, "Wait stopped");
    zassert_equal(ao_storage_stop(&s_test_ao_storage), 0, "Stop on already stopped AO should succeed");

    /* Deinit */
    zassert_equal(ao_storage_deinit(&s_test_ao_storage), 0, "Deinit");
}

ZTEST(storage_suite, test_config_envelope_packing_and_validation)
{
    uint8_t payload[32];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 3 + 7);
    }

    uint8_t env_buf[64];
    size_t env_len = 0;

    /* Pack envelope */
    int ret = config_envelope_pack(CONFIG_DOMAIN_NET_ETH, payload, sizeof(payload),
                                   env_buf, sizeof(env_buf), &env_len);
    zassert_equal(ret, 0, "Pack envelope failed");
    zassert_equal(env_len, CONFIG_ENVELOPE_HEADER_SIZE + sizeof(payload), "Envelope length mismatch");

    /* Unpack envelope */
    uint8_t unpacked[32];
    size_t unpacked_len = 0;
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, 0, "Unpack envelope failed");
    zassert_equal(unpacked_len, sizeof(payload), "Unpacked length mismatch");
    zassert_mem_equal(unpacked, payload, sizeof(payload), "Payload mismatch");

    /* Domain mismatch detection */
    ret = config_envelope_unpack(CONFIG_DOMAIN_HVAC, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should reject domain mismatch");

    /* Buffer too small */
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, 10, &unpacked_len);
    zassert_equal(ret, -ENOBUFS, "Should reject buffer too small");

    /* CRC corruption detection */
    env_buf[CONFIG_ENVELOPE_HEADER_SIZE + 5] ^= 0x55;
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should detect CRC corruption");

    /* Magic corruption detection */
    env_buf[CONFIG_ENVELOPE_HEADER_SIZE + 5] ^= 0x55; /* restore */
    env_buf[0] ^= 0xFF; /* corrupt magic */
    ret = config_envelope_unpack(CONFIG_DOMAIN_NET_ETH, env_buf, env_len,
                                 unpacked, sizeof(unpacked), &unpacked_len);
    zassert_equal(ret, -EBADMSG, "Should detect magic mismatch");
}
