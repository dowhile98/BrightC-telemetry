# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Out-of-tree drivers for custom classes

add_subdirectory_ifdef(CONFIG_ADC_DMA_ENGINE adc_dma)

# Out-of-tree drivers for existing driver classes

