# Design: Modbus Registry and Composition Root (First Bring-Up)

- **Date**: 2026-10-28
- **Status**: Approved
- **Scope**: Modbus RTU subsystem (RS485-1 / RS485-2) and application composition root

## 1. Intention and Rationale

The `app/` layer currently has three problems blocking any growth:

1. **Loose global state**: `modbus_slave_manager.c` keeps the slave table in a file-scope `static` and exposes free functions without an instance. It is an *implicit singleton*: it cannot be substituted in tests nor support two independent buses.
2. **No composition root**: `app/src/infrastructure/di/` contains an empty `.h` and a `.c` with header text. `main.c` starts nothing. Each new module would end up declaring its own `static`.
3. **No isolation between buses**: the current manager does not distinguish the result of a transaction (timeout vs CRC vs Modbus exception) and does not store the electrical configuration of each bus.

The goal is to leave the Modbus subsystem running on a single composition root, with two independent Active Objects, so that any future component (telemetry, HMI, storage) can be added without introducing new global variables.

## 2. Objectives

- A **single static data object** in the entire firmware: `static app_container_t g_app`.
- The slave registry becomes an **injected instance**, not a singleton.
- Two **independent Active Objects**, one per physical bus, with their own stack and queue.
- The registry supports **two buses with different electrical configuration** (baud/parity/stop).
- The result of each transaction is classified (`OK`/`TIMEOUT`/`CRC`/`EXCEPTION`) and feeds separate counters.
- The polling planning and health logic is **pure C** and tested on `native_sim` without hardware or nanoMODBUS.
- `import`/`export` of configuration is **versioned and portable** (without relying on `__attribute__((packed))` or CPU endianness).

## 3. Non-Objectives

- HMI, network telemetry, or persistent storage are not implemented.
- Board DTS is not changed (it is already correct) nor the overlay except as indicated in phase 5.
- CI, linker scripts, or startup code are not touched.
- `malloc` is not introduced: all state is allocated statically in the container.
- Automatic failover is not implemented yet (only the manual `set_bus` operation).

## 4. Anti-Objectives (How We Know It Went Wrong)

- A second static data object appears outside `composition_root.c` and `main.c`.
- The registry requires the caller to hold the lock (lock exposed in the API).
- The AO calls `nmbs_z_*` directly instead of `i_modbus_client`.
- Health logic tests require a UART or `native_posix` with emulation.
- A dead bus degrades the cadence of the other bus.

## 5. Constraints

| Constraint | Source |
|---|---|
| C99/C11, no C++ | `AGENTS.md` |
| Domain without `<zephyr/...>` | `doc/STRUCTURE.md` §3 |
| No heap (`CONFIG_HEAP_MEM_POOL_SIZE=0`) | current build |
| `CONFIG_DCACHE=y` → RX via IRQ, not async | `.config` + nanoMODBUS `known_limitations.md` |
| Hardware DE (RE and DE physically tied) | DTS + user confirmation |
| 512 KB RAM, ~22 KB `.bss` in use | board yaml + ELF `size` |
| DCO sign-off, Conventional Commits | `AGENTS.md` |

## 6. Approaches Considered

### 6.1 Active Object: One Thread vs Two

| Criterion | 1 round-robin thread | **2 threads (chosen)** |
|---|---|---|
| Healthy bus starvation | 16 fallen slaves × 300 ms = 4.8 s stop per 10 s (~50 % of time) | bounded to the bus's own cycle (~1 s) |
| Fault isolation | a hung UART stops both buses | independent |
| Shared state | single writer → no lock | 2 writers → `k_mutex` |
| RAM | 1 stack | 2 stacks (~4 KB = 0.8 % of 512 KB) |
| CPU | identical (both blocked waiting for RX) | identical |

Discarded: one thread with a non-blocking state machine. `nmbs_z_client_*()` is blocking with timeout; RTU framing would have to be reimplemented over the transport.

**Decision: two preemptive `k_thread`s, one `ao_modbus_t` instance per bus.**

### 6.2 Where the Planning Logic Lives

| Option | Verdict |
|---|---|
| Everything in `modbus_registry.c` (with lock) | health logic would be coupled to Zephyr and not testable alone |
| **`slave_scheduler` pure + `modbus_registry` with lock (chosen)** | the decision (`is_due`, health transition) is 100 % testable; the registry only contributes storage and mutual exclusion |
| Domain service with injected vtable | overengineering: pure stateless functions |

### 6.3 Configuration Serialization

| Option | Verdict |
|---|---|
| `memcpy` of `packed` structs | not portable (endianness, compiler padding, no version) |
| **Field-by-field to a little-endian buffer with version header (chosen)** | testable, portable, allows format migration |
| microconf / Zephyr Settings | will be integrated later; the buffer format is the contract |

## 7. Decided Structure

### 7.1 Layers and Files

```
app/include/common/modbus_types.h            DTOs and enums (pure)
app/include/interfaces/i_slave_registry.h    Registry port (vtable + void *impl)
app/include/interfaces/i_modbus_client.h     Modbus client port (vtable + void *impl)

app/src/domain/services/slave_scheduler.{c,h}   Pure C: due / health / backoff
app/src/application/modbus/modbus_registry.{c,h} table + k_mutex + import/export

app/src/infrastructure/adapters/adapter_modbus_nmbs.{c,h}   wraps nmbs_z_client_t
app/src/infrastructure/di/composition_root.{c,h}             composition root
app/src/application/active_objects/ao_modbus.{c,h}           one AO per bus

app/src/main.c                               single static: static app_container_t g_app
```

### 7.2 Container Shape

```c
typedef struct {
    /* Ports */
    i_slave_registry_t      registry;          /* vtable + impl pointing to modbus_registry */
    i_modbus_client_t       client[MODBUS_BUS_MAX];

    /* Adapters by value */
    modbus_registry_t       registry_impl;
    adapter_modbus_nmbs_t   nmbs[MODBUS_BUS_MAX];

    /* Active Objects + static resources */
    ao_modbus_t             ao[MODBUS_BUS_MAX];
    uint8_t                 ao_stack[MODBUS_BUS_MAX][AO_MODBUS_STACK_SIZE];
    struct k_msgq           ao_queue[MODBUS_BUS_MAX];
    uint8_t                 ao_queue_buf[MODBUS_BUS_MAX][AO_MODBUS_QUEUE_DEPTH * sizeof(modbus_event_t)];

    /* Bus configuration (no longer orphaned) */
    modbus_bus_cfg_t        bus_cfg[MODBUS_BUS_MAX];

    uint8_t                 init_mask;         /* bit per subsystem */
} app_container_t;
```

### 7.3 Result Semantics

| Result | Did the slave respond? | `consecutive_fails` | Health |
|---|---|---|---|
| `MODBUS_RESULT_OK` | yes | reset | `ONLINE` |
| `MODBUS_RESULT_TIMEOUT` | no | +1 | `DEGRADED` (1-2) / `OFFLINE_BACKOFF` (≥3) |
| `MODBUS_RESULT_CRC` | no (corrupt frame) | +1 | same as timeout |
| `MODBUS_RESULT_EXCEPTION` | **yes** (Modbus exception code) | no change | stays `ONLINE` |

`EXCEPTION` counts in its own counter: the device is alive but the request is invalid. Marking it as down would hide a configuration error.

### 7.4 Registry API (Summary)

```c
int  modbus_registry_init(modbus_registry_t *self,
                          const modbus_bus_cfg_t cfg[MODBUS_BUS_MAX]);
int  modbus_registry_register(modbus_registry_t *self, const modbus_slave_descriptor_t *desc);
int  modbus_registry_claim_next_due(modbus_registry_t *self, uint8_t bus_id,
                                    uint64_t now_ms, modbus_poll_target_t *out);
int  modbus_registry_record(modbus_registry_t *self, uint8_t device_id,
                            modbus_transaction_result_t result, uint64_t now_ms);
int  modbus_registry_set_bus(modbus_registry_t *self, uint8_t device_id, uint8_t new_bus_id);
int  modbus_registry_get_bus_cfg(const modbus_registry_t *self, uint8_t bus_id,
                                 modbus_bus_cfg_t *out);
int  modbus_registry_snapshot(modbus_registry_t *self, uint8_t bus_id,
                              modbus_slave_snapshot_t *out, size_t max, size_t *out_count);
int  modbus_registry_export(const modbus_registry_t *self, void *buf, size_t max, size_t *out_size);
int  modbus_registry_import(modbus_registry_t *self, const void *buf, size_t size);
```

Rules:
- All locking is **internal**; no function exposes the mutex.
- `claim_next_due` is a single atomic operation (replaces `is_ready_to_poll` + `record_result`), with a per-bus round-robin cursor and stale claim recovery (if an AO dies between claim and record, the slave becomes available again after `AO_MODBUS_CLAIM_STALE_MS`).
- `register` rejects invalid `bus_id`, `slave_addr` outside 1..247, duplicate `device_id`, duplicate address on the same bus, and `MODBUS_MAX_SLAVES_PER_BUS` overflow.
- `import` validates everything above **before** mutating the table (applied on a copy).

### 7.5 Threads

| Parameter | Value | Reason |
|---|---|---|
| Priority | `AO_MODBUS_PRIORITY` (7, preemptive) | never cooperative: an AO in a loop must not block the system |
| Stack | `AO_MODBUS_STACK_SIZE` (2048 B) | `LOG_*` is the largest consumer; will be measured with `CONFIG_THREAD_ANALYZER` |
| Queue | `AO_MODBUS_QUEUE_DEPTH` (8) `modbus_event_t` events | commands (force poll, reconfigure, stop) |
| Cadence | `k_msgq_get(..., timeout = next expiration)` | no fixed `k_sleep`: the AO sleeps until the next slave is *due* |

## 8. Left to the Implementer's Discretion

- Exact field names of `modbus_poll_target_t` and `modbus_event_t`.
- Final `AO_MODBUS_STACK_SIZE` (adjusted after measurement).
- Whether the AO publishes on `zbus` (later phase) or only updates the registry.
- Exact field order of the versioned buffer, respecting: version + counter + fixed-size records + bus configuration.

## 9. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| AO and shell access the registry at the same time | internal locking; `snapshot()` copies and releases |
| AO hangs with a claim taken | recovery via `AO_MODBUS_CLAIM_STALE_MS` |
| Corrupt config import leaves system inoperative | apply on a copy and validate before commit |
| Hot UART reconfiguration breaks DE | uses `nmbs_z_serial_transport_reconfigure()`, never `uart_configure()` with `FLOW_CTRL_NONE` |
| Silent regression: someone adds a `static` | `tools/check_no_globals.sh` script as optional CMake target |
