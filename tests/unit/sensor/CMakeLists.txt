# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_sensor_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_sensor_types.c
    src/test_sensor_matrix_mapper.c
    src/test_fake_sensor_matrix.c
    ${APP_DIR}/app/src/domain/services/sensor_matrix_mapper.c
    ${APP_DIR}/tests/fakes/src/fake_sensor_matrix.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/app/src/domain/services
    ${APP_DIR}/lib/transducers
    ${APP_DIR}/tests/fakes/include
)
