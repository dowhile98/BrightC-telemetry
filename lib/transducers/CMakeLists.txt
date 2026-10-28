# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(
    transducer_rtd.c
    transducer_current_loop.c
    transducer_voltage_divider.c
    transducer_linear.c
)
zephyr_include_directories(.)
