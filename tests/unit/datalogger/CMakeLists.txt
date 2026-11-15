# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_datalogger_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/main.c
    src/test_bkpsram_ring.c
    src/test_ao_datalogger.c
    src/test_datalogger_domain.c
    src/test_telemetry_sampler.c
    src/test_datalogger_streamer.c
    ${APP_DIR}/app/src/domain/storage/bkpsram_ring.c
    ${APP_DIR}/app/src/domain/time/rtc_time_calc.c
    ${APP_DIR}/app/src/domain/datalogger/datalogger_formatters.c
    ${APP_DIR}/app/src/application/active_objects/ao_datalogger.c
    ${APP_DIR}/app/src/application/services/telemetry_sampler.c
    ${APP_DIR}/app/src/application/services/datalogger_streamer.c
    ${APP_DIR}/tests/mocks/fake_bkpsram.c
    ${APP_DIR}/tests/mocks/fake_storage_fs.c
    ${APP_DIR}/tests/mocks/fake_rtc.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/mocks
)
