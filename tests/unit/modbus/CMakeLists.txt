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
    src/test_mbsl32di_codec.c
    src/test_waveshare_relay_codec.c
    src/test_adapter_relay_modbus.c
    ${APP_DIR}/tests/fakes/src/fake_energy_meter.c
    ${APP_DIR}/tests/fakes/src/fake_network.c
    ${APP_DIR}/tests/fakes/src/fake_config_storage.c
    ${APP_DIR}/tests/fakes/src/fake_rtc.c
    ${APP_DIR}/tests/mocks/fake_storage_fs.c
    ${APP_DIR}/tests/mocks/fake_bkpsram.c
    ${APP_DIR}/tests/fakes/src/fake_led.c
    ${APP_DIR}/tests/fakes/src/fake_buzzer.c
    ${APP_DIR}/tests/fakes/src/fake_digital_input.c
    ${APP_DIR}/app/src/domain/services/cwt_tm16pt_codec.c
    ${APP_DIR}/app/src/domain/services/jsy_mk339_codec.c
    ${APP_DIR}/app/src/domain/services/mbsl32di_codec.c
    ${APP_DIR}/app/src/domain/services/waveshare_relay_codec.c
    ${APP_DIR}/app/src/domain/services/sensor_matrix_mapper.c
    ${APP_DIR}/app/src/domain/services/slave_scheduler.c
    ${APP_DIR}/app/src/domain/time/rtc_time_calc.c
    ${APP_DIR}/app/src/domain/storage/sd_lifecycle_sm.c
    ${APP_DIR}/app/src/domain/services/digital_input_aggregator.c
    ${APP_DIR}/app/src/application/modbus/modbus_registry.c
    ${APP_DIR}/app/src/application/modbus/modbus_device_table.c
    ${APP_DIR}/app/src/application/active_objects/ao_modbus.c
    ${APP_DIR}/app/src/application/active_objects/ao_network.c
    ${APP_DIR}/app/src/application/active_objects/ao_storage.c
    ${APP_DIR}/app/src/application/active_objects/ao_storage_sd.c
    ${APP_DIR}/app/src/application/active_objects/ao_datalogger.c
    ${APP_DIR}/app/src/domain/storage/bkpsram_ring.c
    ${APP_DIR}/app/src/application/active_objects/ao_system_indicator.c
    ${APP_DIR}/app/src/application/active_objects/ao_digital_input.c
    ${APP_DIR}/app/src/application/active_objects/ao_sensor_matrix.c
    ${APP_DIR}/app/src/application/zbus/network_channels.c
    ${APP_DIR}/app/src/application/zbus/config_channels.c
    ${APP_DIR}/app/src/application/zbus/storage_channels.c
    ${APP_DIR}/app/src/application/zbus/system_channels.c
    ${APP_DIR}/app/src/application/zbus/indicator_channels.c
    ${APP_DIR}/app/src/application/zbus/digital_input_channels.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_relay_modbus.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_relay_gpio.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_hvac_actuator.c
    ${APP_DIR}/app/src/application/active_objects/ao_hvac.c
    ${APP_DIR}/app/src/application/zbus/hvac_channels.c
    ${APP_DIR}/app/src/application/zbus/sensor_channels.c
    ${APP_DIR}/app/src/domain/models/hvac_unit_entity.c
    ${APP_DIR}/app/src/domain/models/hvac_group_entity.c
    ${APP_DIR}/app/src/infrastructure/di/composition_root.c
    ${APP_DIR}/app/src/application/services/app_facade.c
    ${APP_DIR}/tests/fakes/src/fake_sensor_matrix.c
    ${APP_DIR}/app/src/domain/alarm/alarm_entity.c
    ${APP_DIR}/app/src/domain/alarm/alarm_aggregate.c
    ${APP_DIR}/app/src/domain/alarm/alarm_evaluator.c
    ${APP_DIR}/app/src/domain/alarm/alarm_registry.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_di.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_temp.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_volt.c
    ${APP_DIR}/app/src/infrastructure/adapters/adapter_alarm_detector_hvac.c
    ${APP_DIR}/app/src/application/active_objects/ao_alarm_manager.c
    ${APP_DIR}/app/src/application/zbus/alarm_channels.c
    ${APP_DIR}/app/src/application/active_objects/ao_thermal_energy.c
    ${APP_DIR}/app/src/application/zbus/thermal_channels.c
    ${APP_DIR}/app/src/infrastructure/adapters/sensor_matrix_mapper.c
    ${APP_DIR}/app/src/domain/thermal_energy/humidity_transducer.c
    ${APP_DIR}/app/src/domain/thermal_energy/psychrometrics.c
    ${APP_DIR}/app/src/domain/thermal_energy/thermodynamics.c
    ${APP_DIR}/app/src/domain/thermal_energy/compressor_fsm.c
    ${APP_DIR}/app/src/domain/thermal_energy/predictive_analytics.c
)

target_include_directories(app PRIVATE
    ${APP_DIR}/app/include
    ${APP_DIR}/app/src
    ${APP_DIR}/app/src/domain/services
    ${APP_DIR}/app/src/application/modbus
    ${APP_DIR}/lib
    ${APP_DIR}/lib/codec_modbus
    ${APP_DIR}/lib/transducers
    ${APP_DIR}/tests/fakes/include
    ${APP_DIR}/tests/mocks
)
