# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_hvac_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_hvac_unit.c
    src/test_hvac_group.c
    src/test_adapter_hvac_actuator.c
    src/test_ao_hvac.c
    ${APP_DIR}/app/src/domain/models/hvac_unit_entity.c
    ${APP_DIR}/app/src/domain/models/hvac_group_entity.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_hvac_actuator.c
    ${APP_DIR}/app/src/application/active_objects/ao_hvac.c
    ${APP_DIR}/app/src/application/zbus/hvac_channels.c
    ${APP_DIR}/tests/fakes/src/fake_hvac_unit.c
    ${APP_DIR}/tests/fakes/src/fake_relay.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/fakes/include
)
