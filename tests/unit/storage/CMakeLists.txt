# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_storage_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_config_storage.c
    ${APP_DIR}/tests/fakes/src/fake_config_storage.c
    ${APP_DIR}/app/src/application/zbus/config_channels.c
    ${APP_DIR}/app/src/application/active_objects/ao_storage.c
)



target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/fakes/include
)
