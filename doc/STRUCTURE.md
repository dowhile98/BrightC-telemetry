Estructura de Proyecto Recomendada (T2 + Clean Architecture)

  Esta es la estructura canónica para tu repositorio de aplicación en Zephyr:

    TCS_CurrentInterrupter/
    │
    ├── west.yml                         # Manifiesto T2 de dependencias de Zephyr
    ├── CMakeLists.txt                   # Build system principal de la app
    ├── Kconfig                          # Opciones de configuración de tu aplicación
    ├── prj.conf                         # Configuración base común (Kconfig)
    │
    ├── boards/                          # Configuración de Hardware (Reemplaza a BSP y CubeMX)
    │   ├── nucleo_u575zi_q.overlay      # Pinout, buses (I2C, SPI), displays, GPIOs
    │   ├── nucleo_u575zi_q.conf         # Config Kconfig específica de esta placa
    │   └── custom_board_u575.overlay    # Si tienes tu propia placa diseñada
    │
    ├── dts/                             # (Opcional) Si defines bindings personalizados
    │   └── bindings/                    # YAMLs para componentes propietarios
    │
    ├── include/                         # Puertos e Interfaces (Contratos puros)
    │   ├── interfaces/                  # Interfaces abstractas (V-Tables o tipos opacos)
    │   │   ├── i_relay.h
    │   │   ├── i_gps.h
    │   │   ├── i_storage.h
    │   │   └── i_network.h
    │   └── common/                      # Tipos compartidos agnósticos al HW
    │       └── types.h
    │
    ├── src/
    │   ├── domain/                      # 100% PURO: Lógica de negocio (Sin <zephyr/...>)
    │   │   ├── services/                # CycleScheduler, PPSDispatcher, etc.
    │   │   ├── time/                    # Manejo de UTC, offsets
    │   │   └── models/                  # Entidades del dominio
    │   │
    │   ├── application/                 # Casos de uso y Orquestación
    │   │   ├── active_objects/          # Threads de AO con k_msgq o ZBus
    │   │   │   ├── ao_gps.c
    │   │   │   └── ao_relay.c
    │   │   └── services/                # BacklightService, WiFiEnableService
    │   │
    │   ├── infrastructure/              # Adaptadores a Zephyr y Librerías
    │   │   ├── adapters/                # Implementación de las interfaces
    │   │   │   ├── adapter_relay_gpio.c # Usa <zephyr/drivers/gpio.h>
    │   │   │   ├── adapter_storage_nvs.c# Usa <zephyr/fs/nvs.h> o Settings
    │   │   │   ├── adapter_wifi.c       # Usa Zephyr Wi-Fi / LwIP sockets
    │   │   │   └── adapter_crypto.c     # MbedTLS / Zephyr crypto API
    │   │   └── di/                      # Composition Root (Inyección de dependencias)
    │   │       └── container.c          # Vincula DEVICE_DT_GET con los adaptadores
    │   │
    │   ├── presentation/                # Interfaz de Usuario
    │   │   ├── presenters/              # Lógica MVP de pantallas
    │   │   ├── views/                   # Código de UI (EEZ Studio / LVGL widgets)
    │   │   └── ui_port.c                # Enlace con CONFIG_LVGL
    │   │
    │   └── main.c                       # Punto de entrada (Init de la DI y arranque de AOs)
    │
    └── tests/                           # Tests con Ztest / Twister
        ├── testcase.yaml                # Descriptor de tests para 'twister'
        ├── unit/
        │   ├── domain/                  # Tests unitarios rápidos (corren en native_sim)
        │   └── application/
        └── mocks/                       # Fakes / Mocks para los puertos
  ──────
  ### 4. Implementación de Clean Architecture en Zephyr

  #### A. Devicetree como Hardware Description (Eliminando bsp/ manual)

  En lugar de inicializar GPIOs manualmente con CubeMX:

    /* boards/nucleo_u575zi_q.overlay */
    / {
        relays {
            compatible = "gpio-leds";
            relay_main: relay_0 {
                gpios = <&gpioc 13 GPIO_ACTIVE_HIGH>;
                label = "Main Interrupter Relay";
            };
        };
    };

  #### B. El Adaptador (Infrastructure Layer)

  El adaptador implementa tu interfaz i_relay.h utilizando la macro de Devicetree y el driver de Zephyr:

    /* src/infrastructure/adapters/adapter_relay_gpio.c */
    #include <zephyr/drivers/gpio.h>
    #include "interfaces/i_relay.h"
    
    static const struct gpio_dt_spec relay_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(relay_main), gpios);
    
    static int relay_zephyr_set(bool state) {
        return gpio_pin_set_dt(&relay_spec, state ? 1 : 0);
    }
    
    const struct i_relay_api relay_gpio_adapter = {
        .set_state = relay_zephyr_set,
    };

  #### C. El Dominio (Domain Layer)

  El dominio permanece aislado:

    /* src/domain/services/cycle_scheduler.c */
    #include "interfaces/i_relay.h"
    
    void cycle_scheduler_tick(const struct i_relay_api *relay, uint32_t current_ms) {
        // Lógica pura de cálculo de ciclo
        if (should_open(current_ms)) {
            relay->set_state(false);
        }
    }
  ──────
  ### 5. Configuración de west.yml (Manifiesto T2)

  Tu archivo west.yml en la raíz del repositorio indicará las dependencias exactas que Zephyr y tu proyecto necesitan:

manifest:
  self:
    west-commands: scripts/west-commands.yml

  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos

  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: main
      import:
        # By using name-allowlist we can clone only the modules that are
        # strictly needed by the application.
        name-allowlist:
          - cmsis_6    # required by the ARM port for Cortex-M
          - hal_nordic # required by the custom_plank board (Nordic based)
          - hal_stm32  # required by the nucleo_f302r8 board (STM32 based)
          - fatfs
          - mbedtls
          - mldsa-native
          - tf-psa-crypto
          - mipi-sys-t
          - percepio
          - segger
          - lvgl
          - zcbor
          - nanopb
          - cmsis-dsp
          - cmsis-nn
    - name: microconf
      url: https://github.com/dowhile98/microconf.git
      revision: master
      path: modules/lib/microconf
    - name: nanoMODBUS
      url: https://github.com/dowhile98/nanoMODBUS.git
      revision: master
      path: modules/lib/nanoMODBUS
    - name: zephyr-snmp
      url: https://github.com/dowhile98/zephyr-snmp.git
      revision: main
      path: modules/lib/zephyr-snmp
  ──────
  ### 6. Ventaja Principal para Testing: native_sim

  En tu arquitectura anterior con ThreadX, necesitabas un bsp/mock/ o compilar con GCC de PC simulando el kernel.

  En Zephyr:

  • Puedes ejecutar tus tests usando el target native_sim (POSIX):
    west build -b native_sim tests/unit/domain

