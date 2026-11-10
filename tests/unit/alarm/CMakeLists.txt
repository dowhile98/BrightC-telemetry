# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_alarm_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_alarm_domain.c
    src/test_alarm_ao.c
    src/test_alarm_detectors.c
    ${APP_DIR}/app/src/domain/alarm/alarm_entity.c
    ${APP_DIR}/app/src/domain/alarm/alarm_aggregate.c
    ${APP_DIR}/app/src/domain/alarm/alarm_evaluator.c
    ${APP_DIR}/app/src/domain/alarm/alarm_registry.c
    ${APP_DIR}/app/src/application/zbus/alarm_channels.c
    ${APP_DIR}/app/src/application/zbus/indicator_channels.c
    ${APP_DIR}/app/src/application/active_objects/ao_alarm_manager.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_di.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_temp.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_volt.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_hvac.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
)
