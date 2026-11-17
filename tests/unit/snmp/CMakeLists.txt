# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../../../modules/lib/zephyr-snmp")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_snmp_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_snmp_telemetry.c
    src/test_snmp_traps.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_snmp_telemetry.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_snmp_traps.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/fakes/include
    ${APP_DIR}/../modules/lib/zephyr-snmp/include
)
