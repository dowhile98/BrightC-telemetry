/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

void test_main(void)
{
    ztest_run_all(NULL, false, 1, 1);
}
