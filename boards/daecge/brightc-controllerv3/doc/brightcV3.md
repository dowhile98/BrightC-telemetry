Documentación de Hardware: BrightC-B1 BaseBoard V3

1. Descripción General

El BrightC-B1 BaseBoard V3 es la placa base diseñada para la interconexión, monitoreo y control del sistema. Esta versión (V3) define una arquitectura de hardware avanzada basada en el microcontrolador STM32H743ZIT6 (empaquetado LQFP144), necesaria para el manejo de múltiples interfaces de comunicación (Ethernet, Wi-Fi, RS485), periféricos de almacenamiento, sensores analógicos y eventos externos (sensores/alarmas) a través de expansores I/O y relés.

El objetivo de este documento es establecer el mapeo exacto (pinctrl), la direccionalidad, el propósito y el periférico de hardware (instancia) asignado a cada interfaz física en la placa para su posterior integración con el firmware (Zephyr RTOS / Device Tree).

2. Configuración de Relojes (RCC)

La placa cuenta con las siguientes fuentes de reloj físico:

Oscilador Principal (HSE): Cristal de 25 MHz conectado a los pines PH0 (OSC_IN) y PH1 (OSC_OUT).

Oscilador RTC/Low Power (LSE): Cristal de 32.768 kHz conectado a los pines PC14 (OSC32_IN) y PC15 (OSC32_OUT).

Reloj Ethernet: El PHY LAN8742A cuenta con su propio cristal externo de 25 MHz integrado en su circuitería. Nota: No se requiere exportar un reloj desde el MCU mediante un pin MCO.

3. Asignación de Pines y Multiplexación (Pinctrl)

Nota: Todos los pines referenciados a continuación han sido extraídos del bloque de asignación final del PCB y corresponden a los pines físicos del STM32H743ZIT6.

3.1 Interfaces Seriales (UART/USART)

Interfaz

Función

Pines asignados

Periférico MCU

Notas Adicionales

Consola USB-Serial

TX / RX

PB14 (TX), PB15 (RX)

UART1

Principal para debug y terminal de sistema.

Transceptor RS485-1

Datos / Control

PD5 (TX), PD6 (RX), PD4 (DE)

USART2

Bus diferencial 1 (Requiere control DE/RE).

Pantalla LCD DWIN

TX / RX

PB10 (TX), PB11 (RX)

UART3

Comunicación con interfaz HMI.



Control de energía

PD9 (DWIN_EN), PD8 (DWIN_OVC)

GPIO

Control encendido y flag de sobrecarga.

Módulo Wi-Fi ESP32

TX / RX

PD1 (TX), PD0 (RX)

UART4

Comunicación con coprocesador de red.



Habilitación

PD14 (WIFI_NRST)

GPIO

Reset/Habilitación del ESP32.

Transceptor RS485-2

Datos / Control

PC6 (TX), PC7 (RX), PG8 (DE)

USART6

Bus diferencial 2 (Requiere control DE/RE).

3.2 Interfaces SPI y QuadSPI

Expansores I/O (MultiSPI - Alarmas y Relés):

Periférico MCU: SPI1

Bus SPI: PD7 (MOSI), PG9 (MISO), PA5 (SCK).

Chip Selects (CS): PA15 (CS1), PG3 (CS2), PG2 (CS3), PD15 (CS4).

Interrupciones (Inputs): PE1 (EXP_INT1), PE0 (EXP_INT2), PB9 (EXP_INT3), PB8 (EXP_INT4).

Controlador Ethernet Secundario (W5500):

Periférico MCU: SPI4

Bus SPI: PE14 (MOSI), PE13 (MISO), PE12 (SCK), PE11 (NSS).

Control: PD13 (W5500_INT), PE15 (W5500_RST).

Memoria Flash Externa (QuadSPI):

Periférico MCU: QUADSPI (Bank 1)

Bus de Datos: PF8 (IO0), PD12 (IO1), PE2 (IO2), PF6 (IO3).

Control: PF10 (CLK), PG6 (NSS).

3.3 Otras Interfaces Digitales (I2C, SDMMC, Ethernet, USB)

Bus I²C 1 (Sensores):

Periférico MCU: I2C1

Pines: PB7 (SDA), PB6 (SCL).

Dispositivo: Sensor DHT22.

Bus I²C 2 (Almacenamiento):

Periférico MCU: I2C2

Pines: PF0 (SDA), PF1 (SCL).

Dispositivo: Memoria EEPROM AT24C256C.

Interfaz SDMMC (MicroSD):

Periférico MCU: SDMMC1

Bus de Datos: PC8 (D0), PC9 (D1), PC10 (D2), PC11 (D3).

Control y Detección: PC12 (CLK), PD2 (CMD), PG5 (SW1 / Card Detect).

Interfaz Ethernet LAN principal (RMII):

Periférico MCU: ETH (Ethernet MAC)

Reloj/MDI: PA1 (REF_CLK), PA2 (MDIO), PC1 (MDC).

Datos/Control: PA7 (CRS_DV), PC4 (RXD0), PC5 (RXD1), PG13 (TXD0), PB13 (TXD1), PG11 (TX_EN).

Reset del PHY: PG4 (NRST).

USB OTG FS (Type-C):

Periférico MCU: USB_OTG_FS

Datos: PA11 (DM), PA12 (DP).

Control/Detección: PA10 (ID), PA9 (VBUS).

Gestión de Energía: PD10 (PWR_EN), PG7 (OCVR).

3.4 Entradas Analógicas (ADC) y GPIOs de Propósito General

Mapeo de Sensores de Temperatura (5 pines)
PC0 (CH1): ADC1_INP10 (También disponible en ADC2/ADC3, pero lo agruparemos en ADC1).

PF9 (CH2): ADC3_INP2

PF7 (CH3): ADC3_INP3

PF5 (CH4): ADC3_INP4

PF3 (CH5): ADC3_INP5

Mapeo de Sensores de Presión (10 pines)
PA0 (CH7): ADC1_INP16

PA3 (CH6): ADC1_INP15

PA4 (CH5): ADC1_INP18

PA6 (CH4): ADC1_INP3

PB0 (CH8): ADC1_INP9

PB1 (CH3): ADC1_INP5

PF11 (CH9): ADC1_INP2

PF12 (CH2): ADC1_INP6

PF13 (CH10): ADC2_INP2 (Obligatorio habilitar ADC2)

PF14 (CH1): ADC2_INP6 (Obligatorio habilitar ADC2)


User I/O (Otros GPIOs):

Switch Manual: PD11 (SW_BYPASS).

Botón Usuario: PC13 (USER_PUSH).

LED Usuario: PF2 (USER_LED).

Zumbador: PE5 (BUZZER).

4. Subsistema de Entradas/Salidas Lógicas (Expansores I/O)

La gestión masiva de entradas para alarmas y salidas para cargas de potencia no recae directamente sobre el MCU de forma 1:1, sino que es administrada por la red de Expansores I/O (MCP23S17 en SPI1) descrita en la sección 3.2.

4.1 Entradas (Inputs) - Sistema de Alarmas

Los pines de los expansores configurados como Entradas Digitales (Inputs) monitorean el estado de los sensores. Las alertas de cambio de estado se notifican al MCU a través de las líneas de interrupción de hardware conectadas a los pines PE0, PE1, PB8, PB9.

Etiqueta Lógica

Tipo

Dirección

Descripción / Propósito

ALARM_IN_X

Digital

INPUT

Lectura de estado. Notifica eventos al MCU vía pines EXP_INT.

4.2 Salidas (Outputs) - Control de Relés

Los pines de los expansores configurados como Salidas Digitales (Outputs) activan las etapas de potencia (transistores o arreglos ULN) que a su vez excitan las bobinas de los relés físicos.

Etiqueta Lógica

Tipo

Dirección

Descripción / Propósito

RELAY_OUT_X

Digital

OUTPUT

Señal de activación de Relés. Controlados mediante tramas SPI enviadas desde el MCU.

(Nota: Las salidas deben inicializarse en estado inactivo en el expansor I/O mediante firmware para evitar activaciones indeseadas durante el arranque).

5. Decisiones Arquitectónicas V3

Separación del Bus I²C: El diseño dedica el hardware I2C1 exclusivamente a sensores ambientales (DHT22), mientras que la memoria EEPROM utiliza un bus completamente separado en I2C2.

Reasignación MultiSPI (SPI1): El control de los expansores de puertos (Alarmas y Relés) se centraliza en SPI1, utilizando 4 líneas distintas de Chip Select (CS1 a CS4) para direccionar a cada integrado individualmente en el bus.

Módulo de Redes: Aislamiento total de interfaces. La red cableada principal (RMII) usa el hardware nativo ETH del H7, la red esclava (W5500) cuenta con acceso exclusivo y dedicado mediante SPI4, y el coprocesador Wi-Fi (ESP32) maneja su flujo completo por UART4.
