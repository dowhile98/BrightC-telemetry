# Plan: registro Modbus + Composition Root

> **Para agentes**: ejecución nativa, fase por fase. No avanzar a la fase siguiente sin
> compilar y pasar los tests de la anterior.

**Diseño**: `doc/superpowers/specs/2026-10-28-modbus-registry-composition-root-design.md`

**Objetivo**: sustituir el gestor de esclavos global por un registro inyectado, y montar la
raíz de composición con dos Active Objects (uno por bus RS485).

## Restricciones globales

- Un único `static` de datos: `g_app` en `app/src/main.c`.
- Dominio (`app/src/domain/`) sin `<zephyr/...>`.
- Sin `malloc`. Sin cambios en CI, linker scripts ni arranque.
- Nada de `uart_configure()` con `FLOW_CTRL_NONE` sobre un bus con DE por hardware.
- Identificadores y comentarios de código en inglés; documentación en español.

---

## Fase 1 — Contratos (bloquea todo lo demás)  ✅

- [x] `app/include/common/modbus_types.h`
  - `modbus_transaction_result_t { OK, TIMEOUT, CRC, EXCEPTION, INVALID }`
  - `modbus_bus_cfg_t { baudrate, parity, stop_bits, data_bits }` alineación natural
  - `modbus_poll_target_t { device_id, slave_addr, dev_type, timeout_ms }`
  - `modbus_slave_snapshot_t { descriptor + runtime }`
  - `modbus_slave_health_t`, constantes de backoff
- [x] `app/include/interfaces/i_slave_registry.h` (vtable + `void *impl`)
- [x] `app/include/interfaces/i_modbus_client.h` (vtable + `void *impl`)
- [x] `app/src/domain/services/slave_scheduler.h` (solo declaraciones)
- [x] Andamiaje de test: `tests/unit/modbus/CMakeLists.txt`, `prj.conf`, `tests.yaml`
      (reutilizar el existente, corregir tags a lista YAML)
- [x] Build verde

## Fase 2 — `slave_scheduler` (dominio puro, TDD)  ✅

- [x] Rojo: `tests/unit/modbus/src/test_slave_scheduler.c` con los casos de la tabla
- [x] Verde: `app/src/domain/services/slave_scheduler.c`
- [x] Refactor: sin asignaciones dinámicas, sin estado estático
- [x] `west build -b native_sim tests/unit/modbus -t run` verde

Casos obligatorios:
1. Esclavo nunca sondeado → `due`.
2. `elapsed < poll_interval` → no `due`.
3. `elapsed >= poll_interval` → `due`.
4. 1-2 fallos → `DEGRADED`, sigue con intervalo normal.
5. 3 fallos → `OFFLINE_BACKOFF`, intervalo 10 s.
6. Éxito tras backoff → `ONLINE`, contadores de fallo a cero.
7. `EXCEPTION` no incrementa `consecutive_fails` ni saca de `ONLINE`.
8. `CRC` incrementa `crc_error_count`, no `timeout_count`.
9. `now_ms` con wrap-around (u64) no produce vencimientos espurios.
10. `record` con `now_ms` anterior al último poll no corrompe la salud.

## Fase 3 — `modbus_registry` (aplicación, TDD)  ✅

- [x] Rojo: `tests/unit/modbus/src/test_modbus_registry.c`
- [x] Verde: `app/src/application/modbus/modbus_registry.c`
- [x] Casos: alta/actualización, límites por bus, dirección duplicada, `device_id` duplicado,
      `claim_next_due` round-robin, claim obsoleto, `set_bus` con destino lleno,
      `export`/`import` round-trip, `import` inválido no muta, snapshot
- [x] Test de concurrencia con dos hilos (`k_thread`) sobre `claim_next_due` + `record`
- [x] Tests verdes

## Fase 4 — Composition Root + AO (Zephyr)  ✅

- [x] `app/src/infrastructure/di/composition_root.{c,h}` (`app_container_t`, init, start)
- [x] `app/src/application/active_objects/ao_modbus.{c,h}` (hilo, cola, ciclo claim→poll→record)
- [x] `app/src/main.c` → único `static app_container_t g_app`
- [x] `app/CMakeLists.txt` compila las fuentes nuevas
- [x] Build del firmware verde; dos hilos arrancados (test `composition_root_suite` en `native_sim`)

## Fase 5 — Wiring real  ✅ (HIL pendiente)

- [x] `app/src/infrastructure/adapters/adapter_modbus_nmbs.{c,h}` sobre `nmbs_z_client_t`
- [x] `cwt_tm16pt`: decodificación a `domain/services/`; la capa de E/S se descarta (ver balance: el AO usa el puerto)
- [x] `app/prj.conf`: `CONFIG_NANOMODBUS*`, `CONFIG_UART_INTERRUPT_DRIVEN`, `CONFIG_LOG`
- [x] Overlay: `current-speed` por nodo, `rx-mode="irq"`
- [x] Corrección del include `<codec_modbus/...>` / `lib/CMakeLists.txt`
- [x] Build firmware verde (FLASH 41.908 B / RAM 17.216 B, sin avisos)
- [ ] HIL (flasheo) — **requiere aprobación explícita del usuario**

## Fase 6 — Puertos con wrappers validados  ✅

- [x] `i_slave_registry.h`: un `static inline` validado por operación (7 wrappers)
- [x] `i_modbus_client.h`: un `static inline` validado por operación (4 wrappers)
- [x] Predicados puros reutilizables en `common/modbus_types.h` (bus, dirección, resultado,
      `MODBUS_READ_HOLDING_MAX_REGISTERS`)
- [x] Llamantes migrados (`ao_modbus`, `composition_root`, tests); `api->` sólo en adaptadores y DI
- [x] `tests/unit/modbus/src/test_interfaces.c`: 6 tests que prueban que un handle no enlazado
      o un argumento inválido nunca llegan a la implementación (55 → 61)
- [x] Regla general para puertos futuros documentada en `doc/STRUCTURE.md` §4.1.D
