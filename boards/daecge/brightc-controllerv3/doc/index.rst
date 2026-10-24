.. zephyr:board:: brightc_h743zi

Overview
********

The BrightC-B1 BaseBoard V3 is a custom industrial controller board based
on the STM32H743ZIT6 (LQFP144) Cortex-M7 microcontroller running at 480 MHz.
Designed for automation and monitoring applications, it integrates a wide
range of communication interfaces including Ethernet (built-in MAC + W5500),
RS485, UARTs, SPI, I2C, QSPI flash, and a microSD slot.

Hardware reference: :file:`doc/brightcV3.md` in the board directory.

Hardware
********

- STM32H743ZIT6 in LQFP144 package
- ARM 32-bit Cortex-M7 CPU with FPU
- 480 MHz max CPU frequency
- 2 MB Flash, 1 MB SRAM
- 25 MHz HSE crystal (PH0/PH1)
- 32.768 kHz LSE crystal (PC14/PC15)

On-board peripherals:

- **Ethernet**: built-in RMII MAC with external PHY + W5500 SPI Ethernet
- **RS485** x2: USART2 (PD5/PD6) and USART6 (PC6/PC7) with DE control
- **Console**: USB-to-Serial via UART1 (PB14 TX, PB15 RX) at 115200 8N1
- **DWIN LCD**: USART3 (PB10/PB11) with enable/overcurrent control
- **ESP32 Wi-Fi**: UART4 (PD1/PD0) with GPIO reset
- **SPI1 MultiSPI**: MCP23S17 I/O expanders (4x CS, 4x IRQ)
- **QSPI**: external flash (PF8/PD12/PE2/PF6 IO0-3, PF10 CLK, PG6 NSS)
- **microSD**: SDMMC1 (PC8-11 D0-3, PC12 CLK, PD2 CMD, PG5 card detect)
- **I2C1**: DHT22 temperature/humidity sensor (PB7 SDA, PB6 SCL)
- **I2C2**: AT24C256C EEPROM (PF1 SCL, PF0 SDA)
- **ADC**: multiple temperature (ADC1 PA0-6 + ADC3 PF3-9) and pressure
  sensor inputs
- **USB OTG FS**: PA11 DM, PA12 DP with power control
- **Buzzer**: PWM-driven on TIM15 CH1 (PE5)
- **User LED**: PF2
- **User button**: PC13
- **RTC**, **Watchdog** (IWDG), **True RNG**

Supported Features
==================

.. zephyr:board-supported-hw::

Default Zephyr Peripheral Mapping
---------------------------------

- UART_1 console   : PB14 TX, PB15 RX (115200 8N1)
- UART_2 RS485-1   : PD5 TX, PD6 RX, PD4 DE
- UART_3 DWIN LCD  : PB10 TX, PB11 RX
- UART_4 ESP32     : PD1 TX, PD0 RX
- UART_6 RS485-2   : PC6 TX, PC7 RX, PG8 DE
- I2C1 DHT22       : PB7 SDA, PB6 SCL
- I2C2 EEPROM      : PF1 SCL, PF0 SDA
- SPI1 (MCP23S17)  : PD7 MOSI, PG9 MISO, PA5 SCK
- SPI4 (W5500 ETH) : PE14 MOSI, PE13 MISO, PE12 SCK, PE11 NSS
- QSPI Flash       : PF8 IO0, PD12 IO1, PE2 IO2, PF6 IO3, PF10 CLK, PG6 NSS
- SDMMC1           : PC8 D0, PC9 D1, PC10 D2, PC11 D3, PC12 CLK, PD2 CMD
- ETH RMII         : PA1 REF_CLK, PA2 MDIO, PC1 MDC, PA7 CRS,
                     PC4 RXD0, PC5 RXD1, PG11 TX_EN, PG13 TXD0, PB13 TXD1
- USB OTG FS       : PA11 DM, PA12 DP, PA10 ID, PA9 VBUS
- ADC1 (temp)      : PA0, PA3, PA4, PA6, PB0, PB1, PC0
- ADC3 (temp)      : PF3, PF5, PF7, PF9
- PWM15 buzzer     : PE5 CH1

System Clock
------------

The system clock is driven by the PLL at 480 MHz from a 25 MHz HSE crystal
on PH0/PH1. LSE 32.768 kHz is available for the RTC.

Serial Port
-----------

Console output is assigned to USART1 on PB14 (TX) and PB15 (RX), usually
connected to a USB-to-Serial adapter. Default settings are 115200 8N1.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board is programmed via SWD. Build and flash as usual:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: brightc_h743zi
   :goals: build flash

Debugging
=========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: brightc_h743zi
   :maybe-skip-config:
   :goals: debug

The default flash runner is `stm32cubeprogrammer`. Alternative runners
can be selected with ``--runner`` (openocd, jlink, pyocd).

.. _STM32H743 reference manual:
   https://www.st.com/resource/en/reference_manual/dm00314099.pdf

.. _STM32CubeProgrammer:
   https://www.st.com/en/development-tools/stm32cubeprog.html
