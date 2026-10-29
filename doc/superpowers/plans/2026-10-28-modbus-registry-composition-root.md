# Plan: Modbus Registry + Composition Root

> **For agents**: native execution, phase by phase. Do not advance to the next phase without compiling and passing the previous phase's tests.

**Design**: `doc/superpowers/specs/2026-10-28-modbus-registry-composition-root-design.md`

**Goal**: replace the global slave manager with an injected registry, and set up the composition root with two Active Objects (one per RS485 bus).

## Global Constraints

- A single static data object: `g_app` in `app/src/main.c`.
- Domain (`app/src/domain/`) without `<zephyr/...>`.
- No `malloc`. No changes to CI, linker scripts, or startup.
- No `uart_configure()` with `FLOW_CTRL_NONE` on a bus with hardware DE.
- Code identifiers and comments in English; documentation in Spanish.

---

## Phase 1 — Contracts (blocks everything else)  ✅

- [x] `app/include/common/modbus_types.h`
  - `modbus_transaction_result_t { OK, TIMEOUT, CRC, EXCEPTION, INVALID }`
  - `modbus_bus_cfg_t { baudrate, parity, stop_bits, data_bits }` natural alignment
  - `modbus_poll_target_t { device_id, slave_addr, dev_type, timeout_ms }`
  - `modbus_slave_snapshot_t { descriptor + runtime }`
  - `modbus_slave_health_t`, backoff constants
- [x] `app/include/interfaces/i_slave_registry.h` (vtable + `void *impl`)
- [x] `app/include/interfaces/i_modbus_client.h` (vtable + `void *impl`)
- [x] `app/src/domain/services/slave_scheduler.h` (declarations only)
- [x] Test scaffolding: `tests/unit/modbus/CMakeLists.txt`, `prj.conf`, `tests.yaml`
      (reuse existing, fix tags to YAML list)
- [x] Build green

## Phase 2 — `slave_scheduler` (pure domain, TDD)  ✅

- [x] Red: `tests/unit/modbus/src/test_slave_scheduler.c` with table cases
- [x] Green: `app/src/domain/services/slave_scheduler.c`
- [x] Refactor: no dynamic allocations, no static state
- [x] `west build -b native_sim tests/unit/modbus -t run` green

Mandatory cases:
1. Slave never polled → `due`.
2. `elapsed < poll_interval` → not `due`.
3. `elapsed >= poll_interval` → `due`.
4. 1-2 failures → `DEGRADED`, continues with normal interval.
5. 3 failures → `OFFLINE_BACKOFF`, 10 s interval.
6. Success after backoff → `ONLINE`, failure counters to zero.
7. `EXCEPTION` does not increment `consecutive_fails` nor leave `ONLINE`.
8. `CRC` increments `crc_error_count`, not `timeout_count`.
9. `now_ms` with wrap-around (u64) does not produce spurious expirations.
10. `record` with `now_ms` earlier than last poll does not corrupt health.

## Phase 3 — `modbus_registry` (application, TDD)  ✅

- [x] Red: `tests/unit/modbus/src/test_modbus_registry.c`
- [x] Green: `app/src/application/modbus/modbus_registry.c`
- [x] Cases: create/update, per-bus limits, duplicate address, duplicate `device_id`,
      `claim_next_due` round-robin, stale claim, `set_bus` with full destination,
      `export`/`import` round-trip, invalid `import` does not mutate, snapshot
- [x] Concurrency test with two threads (`k_thread`) on `claim_next_due` + `record`
- [x] Tests green

## Phase 4 — Composition Root + AO (Zephyr)  ✅

- [x] `app/src/infrastructure/di/composition_root.{c,h}` (`app_container_t`, init, start)
- [x] `app/src/application/active_objects/ao_modbus.{c,h}` (thread, queue, claim→poll→record cycle)
- [x] `app/src/main.c` → single `static app_container_t g_app`
- [x] `app/CMakeLists.txt` compiles new sources
- [x] Firmware build green; two threads started (`composition_root_suite` test on `native_sim`)

## Phase 5 — Real Wiring  ✅ (HIL pending)

- [x] `app/src/infrastructure/adapters/adapter_modbus_nmbs.{c,h}` over `nmbs_z_client_t`
- [x] `cwt_tm16pt`: decoding moved to `domain/services/`; I/O layer discarded (see balance: AO uses the port)
- [x] `app/prj.conf`: `CONFIG_NANOMODBUS*`, `CONFIG_UART_INTERRUPT_DRIVEN`, `CONFIG_LOG`
- [x] Overlay: `current-speed` per node, `rx-mode="irq"`
- [x] Correction of include `<codec_modbus/...>` / `lib/CMakeLists.txt`
- [x] Firmware build green (FLASH 41.908 B / RAM 17.216 B, no warnings)
- [ ] HIL (flashing) — **requires explicit user approval**

## Phase 6 — Ports with Validated Wrappers  ✅

- [x] `i_slave_registry.h`: one validated `static inline` per operation (7 wrappers)
- [x] `i_modbus_client.h`: one validated `static inline` per operation (4 wrappers)
- [x] Reusable pure predicates in `common/modbus_types.h` (bus, address, result, `MODBUS_READ_HOLDING_MAX_REGISTERS`)
- [x] Callers migrated (`ao_modbus`, `composition_root`, tests); `api->` only in adapters and DI
- [x] `tests/unit/modbus/src/test_interfaces.c`: 6 tests proving that an unbound handle or invalid argument never reach the implementation (55 → 61)
- [x] General rule for future ports documented in `doc/STRUCTURE.md` §4.1.D
