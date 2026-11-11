# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_thermal_energy_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

target_sources(app PRIVATE
    src/test_humidity_transducer.c
    src/test_psychrometrics.c
    src/test_thermodynamics.c
    src/test_compressor_fsm.c
    src/test_predictive_ols.c
    src/test_thermal_energy_port.c
    src/test_sensor_matrix_mapper.c
    src/test_ao_thermal_energy.c
    ${APP_DIR}/tests/fakes/src/fake_thermal_energy.c
    ${APP_DIR}/app/src/domain/thermal_energy/humidity_transducer.c
    ${APP_DIR}/app/src/domain/thermal_energy/psychrometrics.c
    ${APP_DIR}/app/src/domain/thermal_energy/thermodynamics.c
    ${APP_DIR}/app/src/domain/thermal_energy/compressor_fsm.c
    ${APP_DIR}/app/src/domain/thermal_energy/predictive_analytics.c
    ${APP_DIR}/app/src/infrastructure/adapters/sensor_matrix_mapper.c
    ${APP_DIR}/app/src/application/zbus/thermal_channels.c
    ${APP_DIR}/app/src/application/zbus/sensor_channels.c
    ${APP_DIR}/app/src/application/zbus/digital_input_channels.c
    ${APP_DIR}/app/src/application/active_objects/ao_thermal_energy.c
    ${APP_DIR}/app/src/application/config/default_config_profiles.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/tests/fakes/include
)
