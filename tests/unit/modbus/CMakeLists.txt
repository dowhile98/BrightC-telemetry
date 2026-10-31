# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_modbus_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_modbus_types.c
    src/test_interfaces.c
    src/test_cwt_tm16pt.c
    src/test_jsy_mk339_codec.c
    src/test_slave_scheduler.c
    src/test_modbus_registry.c
    src/test_composition_root.c
    ${APP_DIR}/tests/fakes/src/fake_energy_meter.c
    ${APP_DIR}/app/src/domain/services/cwt_tm16pt_codec.c
    ${APP_DIR}/app/src/domain/services/jsy_mk339_codec.c
    ${APP_DIR}/app/src/domain/services/slave_scheduler.c
    ${APP_DIR}/app/src/application/modbus/modbus_registry.c
    ${APP_DIR}/app/src/application/active_objects/ao_modbus.c
    ${APP_DIR}/app/src/infrastructure/di/composition_root.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/app/src/domain/services
    ${APP_DIR}/app/src/application/modbus
    ${APP_DIR}/lib
    ${APP_DIR}/lib/codec_modbus
    ${APP_DIR}/tests/fakes/include
)
