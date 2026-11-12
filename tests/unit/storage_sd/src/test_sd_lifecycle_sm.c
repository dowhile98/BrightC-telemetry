/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for SD Lifecycle Finite State Machine.
 */

#include <zephyr/ztest.h>

#include "domain/storage/sd_lifecycle_sm.h"

static sd_lifecycle_sm_t s_sm;

static void *sd_sm_setup(void)
{
    sd_lifecycle_sm_init(&s_sm);
    return NULL;
}

static void sd_sm_before(void *fixture)
{
    (void)fixture;
    sd_lifecycle_sm_init(&s_sm);
}

ZTEST_SUITE(sd_lifecycle_sm_suite, NULL, sd_sm_setup, sd_sm_before, NULL, NULL);

ZTEST(sd_lifecycle_sm_suite, test_sm_init_state)
{
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_NOT_INSERTED);
    zassert_false(sd_lifecycle_sm_is_ready(&s_sm));
    zassert_false(sd_lifecycle_sm_is_card_present(&s_sm));
}

ZTEST(sd_lifecycle_sm_suite, test_sm_card_insert_and_debounce)
{
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_START_DEBOUNCE);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_INSERTED_UNMOUNTED);
    zassert_true(sd_lifecycle_sm_is_card_present(&s_sm));
    zassert_false(sd_lifecycle_sm_is_ready(&s_sm));

    /* Tick 50 ms - not yet expired */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 50);
    zassert_equal(act, SD_LIFECYCLE_ACTION_NONE);

    /* Tick another 50 ms (total 100 ms) */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 50);
    zassert_equal(act, SD_LIFECYCLE_ACTION_NONE);

    /* Tick another 50 ms (total 150 ms) -> triggers mount! */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 50);
    zassert_equal(act, SD_LIFECYCLE_ACTION_MOUNT);
}

ZTEST(sd_lifecycle_sm_suite, test_sm_debounce_bounce_cancellation)
{
    /* Card inserted */
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_START_DEBOUNCE);

    /* Contact chatter: removed after 40 ms */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 40);
    zassert_equal(act, SD_LIFECYCLE_ACTION_NONE);

    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_REMOVED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_PUBLISH_STATUS);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_NOT_INSERTED);
    zassert_false(sd_lifecycle_sm_is_card_present(&s_sm));

    /* Subsequent tick does not trigger mount */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 200);
    zassert_equal(act, SD_LIFECYCLE_ACTION_NONE);
}

ZTEST(sd_lifecycle_sm_suite, test_sm_mount_success_and_ready)
{
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 150);

    /* Mount succeeds */
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_MOUNT_SUCCESS, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_PUBLISH_STATUS);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_MOUNTED);
    zassert_true(sd_lifecycle_sm_is_ready(&s_sm));
    zassert_equal(s_sm.last_error, 0);
}

ZTEST(sd_lifecycle_sm_suite, test_sm_mount_failure_and_error)
{
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 150);

    /* Mount fails with -EIO */
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_MOUNT_FAILED, -EIO);
    zassert_equal(act, SD_LIFECYCLE_ACTION_PUBLISH_STATUS);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_ERROR);
    zassert_false(sd_lifecycle_sm_is_ready(&s_sm));
    zassert_equal(s_sm.last_error, -EIO);
}

ZTEST(sd_lifecycle_sm_suite, test_sm_orderly_eject)
{
    /* Reach MOUNTED state */
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 150);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_MOUNT_SUCCESS, 0);
    zassert_true(sd_lifecycle_sm_is_ready(&s_sm));

    /* Request eject */
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_EJECT_REQUESTED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_UNMOUNT);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_EJECT_REQUESTED);

    /* Unmount complete */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_PUBLISH_STATUS);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_INSERTED_UNMOUNTED);
    zassert_false(sd_lifecycle_sm_is_ready(&s_sm));
    zassert_true(sd_lifecycle_sm_is_card_present(&s_sm));

    /* Manual remount */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_MANUAL_MOUNT, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_MOUNT);
}

ZTEST(sd_lifecycle_sm_suite, test_sm_surprise_removal_while_mounted)
{
    /* Reach MOUNTED state */
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_DETECTED, 0);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_TICK, 150);
    sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_MOUNT_SUCCESS, 0);
    zassert_true(sd_lifecycle_sm_is_ready(&s_sm));

    /* Surprise removal */
    sd_lifecycle_action_t act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_CARD_REMOVED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_UNMOUNT);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_EJECT_REQUESTED);
    zassert_false(sd_lifecycle_sm_is_card_present(&s_sm));

    /* Unmount completes */
    act = sd_lifecycle_sm_process(&s_sm, SD_LIFECYCLE_EVT_UNMOUNT_COMPLETED, 0);
    zassert_equal(act, SD_LIFECYCLE_ACTION_PUBLISH_STATUS);
    zassert_equal(sd_lifecycle_sm_get_status(&s_sm), STORAGE_FS_CARD_NOT_INSERTED);
    zassert_false(sd_lifecycle_sm_is_ready(&s_sm));
}
