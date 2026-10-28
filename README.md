# BrightC Telemetry Firmware

[![Build](https://github.com/daecge/BrightC-telemetry/actions/workflows/build.yml/badge.svg)](https://github.com/daecge/BrightC-telemetry/actions/workflows/build.yml)
[![Architecture: Clean Architecture](https://img.shields.io/badge/Architecture-Clean%20Architecture%20%2F%20Hexagonal-blue.svg)](doc/STRUCTURE.md)
[![Testing: TDD + Twister](https://img.shields.io/badge/Testing-TDD%20%7C%20Ztest%20%7C%20Twister-brightgreen.svg)](tests/)
[![Target MCU](https://img.shields.io/badge/MCU-STM32H743ZIT6%20LQFP144-orange.svg)](boards/daecge/brightc-controllerv3/doc/brightcV3.md)
[![RTOS: Zephyr](https://img.shields.io/badge/RTOS-Zephyr%20RTOS-purple.svg)](https://zephyrproject-rtos.org)

Firmware de grado industrial para la plataforma **DAECGE BrightC Controller V3**, diseñado para adquisición de telemetría de alta confiabilidad, monitoreo de energía, comunicaciones industriales (RS485/Modbus, Ethernet, Wi-Fi/BLE) y control de campo.

El proyecto está construido desde sus cimientos siguiendo los principios de **Clean Architecture (Puertos y Adaptadores)** y **Desarrollo Guiado por Pruebas (TDD)**, explotando nativamente el ecosistema de **Zephyr RTOS** (Zbus, Devicetree, Kconfig, SMF, Settings y Shell).

---

## 🚀 Características Principales

- **Hardware de Control**: Microcontrolador STM32H743ZIT6 (Cortex-M7 @ 480 MHz, 2MB Flash, 1MB RAM).
- **Clean Architecture & Inversión de Dependencias**:
  - Dominio 100% puro en C (agnóstico a Zephyr y al silicio).
  - Puertos abstractos (`i_*.h`) para periféricos y almacenamiento.
  - Adaptadores que implementan los puertos consumiendo drivers nativos de Zephyr.
- **TDD (Test-Driven Development)**:
  - Pruebas unitarias de dominio y aplicación en la máquina host con `native_sim` (POSIX) en milisegundos.
  - Fakes en memoria para aislar lógica sin depender de hardware físico.
  - Automatización con `twister` y métricas de cobertura de código con `lcov/gcov`.
- **Comunicaciones y Telemetría Multicanal**:
  - **Modbus RTU / Industrial**: Transceptores RS485-1 (USART2) y RS485-2 (USART6) vía `nanoMODBUS`.
  - **Ethernet 10/100**: PHY LAN8742A con pila TCP/IP nativa de Zephyr.
  - **Wi-Fi & Bluetooth LE**: Módulo ESP32 coprocesador vía protocolo ESP-Hosted.
  - **HMI / Pantalla**: Panel LCD serial DWIN en UART3 con patrón Model-View-Presenter (MVP).
  - **Monitoreo & SNMP**: Soporte para `zephyr-snmp` y micro-configurador `microconf`.
- **Desacoplamiento con Zephyr Zbus**:
  - Bus de eventos asíncrono y con seguridad de tipos en tiempo de compilación para la distribución de muestras y alarmas entre hilos (Active Objects).
- **Consola Interactiva Zephyr Shell**:
  - CLI para diagnóstico de campo, inspección de variables y prueba de relés por UART1 USB-Serial.

---

## 📁 Estructura del Proyecto

Para una explicación exhaustiva de qué va en cada carpeta, las reglas de arquitectura y el árbol de decisión, consulta la [**Guía Maestra de Estructura (doc/STRUCTURE.md)**](doc/STRUCTURE.md).

```text
BrightC-telemetry/
├── app/                     # Aplicación principal
│   ├── include/interfaces/  # Puertos abstractos (i_modbus.h, i_storage.h, i_relay.h)
│   ├── src/domain/          # Dominio PURO (C99/C11 sin <zephyr/...>)
│   ├── src/application/     # Casos de uso, Active Objects y canales Zephyr Zbus
│   ├── src/infrastructure/  # Adaptadores a drivers Zephyr y Composition Root
│   ├── src/presentation/    # MVP pantalla DWIN y comandos Zephyr Shell
│   ├── boards/              # Overlays DTS específicos de la app
│   └── prj.conf             # Configuración Kconfig de la aplicación
├── boards/                  # Definición de hardware de la placa BrightC V3
├── drivers/                 # Drivers Zephyr fuera del árbol (out-of-tree)
├── lib/                     # Librerías auxiliares
├── tests/                   # Suites de pruebas TDD (unitarias en native_sim, fakes, HIL)
└── doc/                     # Documentación técnica de arquitectura y hardware
```

---

## 🛠️ Requisitos Previos e Inicialización

### 1. Entorno Zephyr
Asegúrate de contar con el Zephyr SDK y la herramienta `west` instalada según la [Guía Oficial de Zephyr](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

### 2. Inicializar el Workspace (Topología West T2)
```bash
# Inicializar el workspace en una carpeta dedicada
west init -m https://github.com/daecge/BrightC-telemetry --mr main BrightC_TelemetryWorkspace
cd BrightC_TelemetryWorkspace

# Descargar Zephyr RTOS y todos los módulos dependientes (nanoMODBUS, microconf, hal_stm32, etc.)
west update
```

---

## 🔨 Compilación y Ejecución

### Compilar para la placa física (BrightC Controller V3)
```bash
cd BrightC-telemetry

# Compilación base
west build -b brightc-controllerv3 app

# Compilación con opciones de depuración y logs verbosos
west build -b brightc-controllerv3 app -- -DEXTRA_CONF_FILE=debug.conf
```

### Flashear el hardware (ST-Link / OpenOCD)
Conecta tu programador ST-Link al conector SWD de la placa:
```bash
west flash --runner openocd
```

---

## 🧪 Estrategia TDD y Ejecución de Pruebas

Toda la lógica de dominio se desarrolla mediante el ciclo **Red-Green-Refactor**. Las pruebas unitarias no requieren el microcontrolador físico y corren instantáneamente sobre el target POSIX `native_sim`.

```bash
# 1. Ejecutar todas las pruebas unitarias en native_sim
west twister -T tests/unit -p native_sim -v --inline-logs

# 2. Ejecutar pruebas con reporte de cobertura de código
west twister -T tests/unit -p native_sim --coverage -v

# 3. Ejecutar pruebas de integración con emuladores Zephyr
west twister -T tests/integration -p native_sim
```

---

## 📖 Documentación

- [**Guía de Arquitectura y Estructura (doc/STRUCTURE.md)**](doc/STRUCTURE.md) — Explicación paso a paso de capas, patrones IoC, Zbus, Fakes y reglas de gobernanza.
- [**Referencia de Hardware (boards/daecge/brightc-controllerv3/doc/brightcV3.md)**](boards/daecge/brightc-controllerv3/doc/brightcV3.md) — Mapeo de pines, transceptores, clocks y esquemáticos.
- [**Devicetree de la Placa (boards/daecge/brightc-controllerv3/brightc-controllerv3.dts)**](boards/daecge/brightc-controllerv3/brightc-controllerv3.dts) — Definición de hardware DTS.

---

## ⚖️ Licencia

Distribuido bajo la licencia [Apache 2.0](LICENSE).  
Copyright (c) 2026 DAECGE.
