#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/application/active_objects/ao_modbus.c
    src/application/active_objects/ao_network.c
    src/application/active_objects/ao_sensor_matrix.c
    src/application/zbus/network_channels.c
    src/application/modbus/modbus_registry.c
    src/domain/services/cwt_tm16pt_codec.c
    src/domain/services/jsy_mk339_codec.c
    src/domain/services/mbsl32di_codec.c
    src/domain/services/sensor_matrix_mapper.c
    src/domain/services/thermodynamic_engine.c
    src/domain/services/slave_scheduler.c
    src/domain/services/waveshare_relay_codec.c
    src/application/active_objects/ao_storage.c
    src/application/active_objects/ao_system_indicator.c
    src/application/active_objects/ao_digital_input.c
    src/application/zbus/config_channels.c
    src/application/zbus/indicator_channels.c
    src/application/zbus/digital_input_channels.c
    src/domain/services/digital_input_aggregator.c
    src/infrastructure/adapters/adapter_led_gpio.c
    src/infrastructure/adapters/adapter_buzzer_pwm.c
    src/infrastructure/adapters/adapter_di_gpio.c
    src/infrastructure/adapters/adapter_relay_gpio.c
    src/infrastructure/adapters/adapter_sensor_zephyr.c
    src/infrastructure/di/composition_root.c
    src/domain/models/hvac_unit_entity.c
    src/domain/models/hvac_group_entity.c
    src/application/zbus/hvac_channels.c
    src/application/zbus/sensor_channels.c
    src/infrastructure/adapters/adapter_hvac_actuator.c
    src/application/active_objects/ao_hvac.c
    src/application/services/app_facade.c
    src/domain/alarm/alarm_entity.c
    src/domain/alarm/alarm_aggregate.c
    src/domain/alarm/alarm_evaluator.c
    src/domain/alarm/alarm_registry.c
    src/application/zbus/alarm_channels.c
    src/application/active_objects/ao_alarm_manager.c
    src/infrastructure/adapters/adapter_alarm_detector_di.c
    src/infrastructure/adapters/adapter_alarm_detector_temp.c
    src/infrastructure/adapters/adapter_alarm_detector_volt.c
    src/infrastructure/adapters/adapter_alarm_detector_hvac.c
    src/domain/thermal_energy/humidity_transducer.c
    src/domain/thermal_energy/psychrometrics.c
    src/domain/thermal_energy/thermodynamics.c
    src/domain/thermal_energy/compressor_fsm.c
    src/domain/thermal_energy/predictive_analytics.c
    src/infrastructure/adapters/sensor_matrix_mapper.c
    src/application/zbus/thermal_channels.c
    src/application/active_objects/ao_thermal_energy.c
    src/domain/time/rtc_time_calc.c
    src/infrastructure/adapters/adapter_rtc_zephyr.c
    src/domain/storage/sd_lifecycle_sm.c
    src/application/zbus/storage_channels.c
    src/application/active_objects/ao_storage_sd.c
    src/infrastructure/adapters/adapter_storage_sd_zephyr.c
    src/infrastructure/adapters/adapter_modbus_tcp_server.c
    src/application/active_objects/ao_modbus_tcp.c
)

if(CONFIG_SHELL)
    target_sources(app PRIVATE
        src/presentation/shell/shell_brightc.c
    )
endif()

if(CONFIG_MICROCONF)
    target_sources(app PRIVATE
        src/infrastructure/adapters/adapter_storage_microconf.c
    )
endif()

if(CONFIG_NETWORKING)
    target_sources(app PRIVATE
        src/infrastructure/adapters/adapter_eth_stm32.c
        src/infrastructure/adapters/adapter_wifi_esp_hosted.c
    )
endif()

target_include_directories(app PRIVATE
    include
    src
    lib
    # Public API of the out-of-tree ADC DMA engine. The header is always on the
    # include path so the composition root can reference the port-agnostic
    # adapter type even on platforms where the driver is not built.
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/adc_dma/include
)

# The nanoMODBUS transport adapter is the only part that depends on the stack,
# so it is only compiled when the stack is enabled.
if(CONFIG_NANOMODBUS)
    target_sources(app PRIVATE
        src/infrastructure/adapters/adapter_modbus_nmbs.c
        src/infrastructure/adapters/adapter_relay_modbus.c
    )
endif()

# Bring-up diagnostic for a silent Modbus slave. Off unless the build asks for
# it, so a normal image carries no probe code at all.
if(CONFIG_APP_MODBUS_BUS_PROBE)
    target_sources(app PRIVATE src/application/diagnostics/modbus_bus_probe.c)
endif()

# The code below locates the git index file for this repository and adds it as a dependency for
# the application VERSION file so that if the repo has a new commit added, even if no files in
# the build have changed, the application version file will be regenerated with the new git commit
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
    WORKING_DIRECTORY .
    OUTPUT_VARIABLE application_git_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE stderr
    RESULT_VARIABLE return_code
  )
  # If there is an error e.g. it is not a git repo, we just silently ignore it and continue
  # without a dependency, this will be the case with freestanding applications.
  if(NOT return_code)
    if(NOT "${stderr}" STREQUAL "")
      message(WARNING "Application build version git rev-parse warned: ${stderr}")
    endif()

    set_property(TARGET app_version_h PROPERTY APP_VERSION_DEPENDS ${application_git_dir}/index)
  endif()
endif()
