# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_digital_input_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_digital_input.c
    ${APP_DIR}/app/src/domain/services/digital_input_aggregator.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_di_gpio.c
    ${APP_DIR}/app/src/application/zbus/digital_input_channels.c
    ${APP_DIR}/app/src/application/active_objects/ao_digital_input.c
    ${APP_DIR}/tests/fakes/src/fake_digital_input.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/fakes/include
)
