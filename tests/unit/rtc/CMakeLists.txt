# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_rtc_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/main.c
    src/test_rtc_calc.c
    src/test_fake_rtc.c
    ${APP_DIR}/app/src/domain/time/rtc_time_calc.c
    ${APP_DIR}/tests/mocks/fake_rtc.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/mocks
)
