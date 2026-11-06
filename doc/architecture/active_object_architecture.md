# Active Object Architecture Specification

- **Status**: Official Architecture Standard & Canonical Template
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Environment**: Zephyr RTOS (West T2 Topology)
- **Layer**: Application Layer (Ring ② — Concurrency Orchestration & Process Lifecycle)
- **Language**: English (per project convention)

---

## 1. Executive Summary & Problem Statement

### 1.1 Purpose of Active Objects
In the **BrightC Telemetry** architecture, an **Active Object (AO)** decouples method invocation from method execution. Each Active Object encapsulates:
1. **A dedicated execution thread (`struct k_thread`)**: Owns an independent priority and call stack.
2. **An asynchronous event queue (`struct k_msgq`) or synchronization gate**: Serializes incoming requests, commands, and events.
3. **A private state machine or execution loop**: Guarantees that business logic and external I/O execute in a single, predictable context with zero shared-state race conditions.

> **Absolute Rule**: Any hardware I/O, network communication, Modbus transaction, sensor polling, or non-volatile flash write that can block or take **> 1 ms** MUST execute inside an Active Object thread, never in the caller's context (e.g., ISR, Shell, Zbus listener, or higher-level business services).

### 1.2 Identified Problems in Legacy Implementations
Prior implementations exhibited the following architectural anti-patterns:
1. **Dynamic Thread Lifecycle (`k_thread_create` inside `start()`)**: Creating threads dynamically on `start()` and calling `k_thread_join()` / `k_thread_abort()` on `stop()` introduces race conditions and potential thread control block (TCB) corruption. Re-instantiating threads over already used TCBs violates determinism.
2. **MPU Stack Guard Alignment Risks**: On Cortex-M7 with `CONFIG_HW_STACK_PROTECTION` and `CONFIG_MPU_STACK_GUARD`, thread stacks must be statically allocated and hardware-aligned at file scope. Thread recreation increases vulnerability to stack guard misalignments.
3. **File-Scope Mutable State**: Hidden static buffers (such as serialization scratchpads) at file scope violate the Clean Architecture invariant that `app_container_t` in `main.c` is the only mutable static instance.
4. **Non-Uniform Shutdown Semantics**: Absence of a standardized `wait_stopped()` and `deinit()` mechanism makes graceful shutdown and failover recovery unpredictable.

---

## 2. Canonical 5-Phase Lifecycle Contract

Every Active Object in the system MUST strictly implement the following standardized lifecycle functions:

```
┌────────────────────────────────────────────────────────────────────────┐
│  UNINITIALIZED ──Init()──► READY ──Start()──► RUNNING (inner loop)     │
│                          │ ◄                   │                       │
│                          │ blocked on gate     │ Stop()                │
│                          │                     ▼                       │
│                       Deinit() ◄────── STOPPING (outer loop)           │
│                          │               WaitStopped()                 │
│                          │                     │                       │
│                          │  Start() (re-arm)   ▼                       │
│                          └───────────────── READY                      │
└────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Lifecycle State Transitions

| Function | Transition | Description & Responsibilities |
|---|---|---|
| `ao_<name>_init(self, config, ...)` | `UNINITIALIZED -> READY` | - Clears instance structure (`memset`).<br>- Validates injected dependency interfaces and configuration parameters.<br>- Initializes synchronization primitives (`run_sem`, `stopped_sem`, `k_msgq`).<br>- **Spawns the Zephyr thread once** (blocked immediately on the gate primitive).<br>- Sets state to `READY`. |
| `ao_<name>_start(self)` | `READY -> RUNNING` | - Validates that the instance is in `READY` state.<br>- Sets `is_running = true`.<br>- **Opens the gate** (`k_sem_give(&run_sem)` for Pattern A, or posts `AO_MSG_START` for Pattern B).<br>- The thread unblocks and enters its active `inner loop`. |
| `ao_<name>_stop(self)` | `RUNNING -> STOPPING` | - Sets `is_running = false`.<br>- Posts unblocking control signal (`AO_MSG_STOP` or wakes the sleep).<br>- **DOES NOT abort or kill the thread**; the thread cooperatively completes its current cycle, exits the `inner loop`, and signals `stopped_sem`. |
| `ao_<name>_wait_stopped(self, timeout)` | `STOPPING -> READY` | - Blocks the caller until the thread confirms it has stopped via `k_sem_take(&stopped_sem, timeout)`.<br>- Enables clean, deterministic shutdown and re-arm without blind delays. |
| `ao_<name>_deinit(self)` | `READY -> UNINITIALIZED` | - Can only be called when the AO is in `READY` state (`!is_running`).<br>- Signals permanent termination (`terminate = true` or `AO_MSG_TERMINATE`).<br>- Awakes the outer loop and joins/waits on `stopped_sem`.<br>- Deletes or resets kernel objects and clears the context structure. |

> [!IMPORTANT]
> **The Thread is Created ONCE in `Init()` and Never Aborted**:
> The worker thread uses a persistent **Double-Loop architecture** (`outer loop` + `inner loop`). Between stop and start cycles, the thread sits safely blocked on the gate semaphore or message queue consuming zero CPU cycles.

---

## 3. The Three Canonical Active Object Archetypes

Depending on the operational nature of the subsystem, every Active Object MUST implement one of the three canonical patterns:

```text
                     ACTIVE OBJECT ARCHETYPES
                                │
        ┌───────────────────────┼────────────────────────┐
        ▼                       ▼                        ▼
    Pattern A               Pattern B                Pattern C
 Periodic Polling         Message-Driven          Scheduled Arbiter
(Digital Inputs, Watchdog) (Network, Storage, UI)    (Modbus Master)
 Outer: run_sem          Outer: MSG_START         Outer: MSG_START
 Inner: k_msleep         Inner: Queue Receive     Inner: Dynamic Timeout
```

---

### 3.1 Pattern A: Periodic Polling Active Object (Gate Semaphore)

#### Use Cases
High-frequency or periodic sampling without incoming message commands (e.g., `ao_digital_input` at 50 Hz, hardware thermal watchdog).

#### Concurrency Model
- **Gate Primitive**: Binary semaphore `struct k_sem run_sem` (initialized to 0, limit 1).
- **Termination / Confirmation**: Binary semaphore `struct k_sem stopped_sem` (initialized to 0, limit 1).
- **Execution Loop**:
  ```text
  Thread Entry:
    while (!self->terminate) {
        k_sem_take(&self->run_sem, K_FOREVER);  // Gate: wait for Start()
        if (self->terminate) break;

        while (self->is_running) {              // Inner work loop
            ao_<name>_step(self, elapsed_ms);
            k_msleep(self->poll_period_ms);
        }

        k_sem_give(&self->stopped_sem);         // Confirms stop to WaitStopped()
    }
    k_sem_give(&self->stopped_sem);             // Confirms exit to Deinit()
  ```

---

### 3.2 Pattern B: Message-Driven Active Object (Queue Gate)

#### Use Cases
Event-driven subsystems triggered by asynchronous messages or commands (e.g., `ao_storage` persisting configs, `ao_network` interface reconfiguration, `ao_system_indicator` alarm notifications).

#### Concurrency Model
- **Gate Primitive**: Command Queue `struct k_msgq`. The queue itself acts as the gate.
- **Reserved System Control Messages**:
  ```c
  typedef enum {
      AO_MSG_NONE      = 0,
      AO_MSG_START     = 1, /* Posted by ao_<name>_start() */
      AO_MSG_STOP      = 2, /* Posted by ao_<name>_stop() */
      AO_MSG_TERMINATE = 3, /* Posted by ao_<name>_deinit() */
      AO_MSG_USER_BASE = 4, /* Application-specific events start here */
  } ao_msg_type_t;
  ```
- **Execution Loop**:
  ```text
  Thread Entry:
    while (!should_terminate) {
        k_msgq_get(self->queue, &msg, K_FOREVER); // Gate: wait for MSG_START
        if (msg.type == AO_MSG_TERMINATE) break;
        if (msg.type != AO_MSG_START) continue;

        while (self->is_running) {               // Inner work loop
            int ret = k_msgq_get(self->queue, &msg, timeout);
            if (ret == 0) {
                if (msg.type == AO_MSG_STOP) break;
                if (msg.type == AO_MSG_TERMINATE) {
                    should_terminate = true;
                    break;
                }
                ao_<name>_dispatch(self, &msg);
            } else if (ret == -EAGAIN) {
                ao_<name>_on_timeout(self);      // Optional periodic tick
            }
        }

        self->is_running = false;
        if (!should_terminate) {
            k_sem_give(&self->stopped_sem);      // Confirms stop to WaitStopped()
        }
    }
    k_sem_give(&self->stopped_sem);              // Confirms exit to Deinit()
  ```

---

### 3.3 Pattern C: Scheduled Arbiter Active Object (Dynamic Timeout)

#### Use Cases
Shared physical buses with hybrid scheduling: periodic slave polling combined with immediate high-priority asynchronous commands (e.g., `ao_modbus` orchestrating RS-485 transactions and asynchronous coil writes).

#### Concurrency Model
- Implements the outer gate of Pattern B (`AO_MSG_START`).
- In the inner loop, evaluates the next due timestamp via `i_slave_registry_claim_next_due()`.
- Calculates remaining idle time: `idle_ms = next_due - now`.
- Performs `k_msgq_get(self->queue, &msg, K_MSEC(idle_ms))`:
  - If a message arrives: executes immediately (e.g., relay toggle command).
  - If timeout expires: executes the scheduled Modbus poll transaction.

---

## 4. Architectural Invariants & Safety Rules

Every Active Object implementation MUST comply with the following invariants:

### 4.1 MPU Stack Guard Safety (Cortex-M7 Invariant)
```c
/* CORRECT: Defined at file scope with Zephyr alignment macros */
static K_THREAD_STACK_DEFINE(s_ao_worker_stack, AO_WORKER_STACK_SIZE);

/* STRICTLY FORBIDDEN: Embedding k_thread_stack_t inside structs */
typedef struct {
    k_thread_stack_t stack[AO_WORKER_STACK_SIZE]; /* FATAL: Triggers MPU FAULT on H7 */
} broken_container_t;
```
Zephyr hardware stack protection (`CONFIG_HW_STACK_PROTECTION` / `CONFIG_MPU_STACK_GUARD`) enforces hardware MPU alignment for thread stacks. Stacks MUST be defined at file scope (e.g., in `composition_root.c`) and passed as pointers (`k_thread_stack_t *stack, size_t stack_size`) to `ao_<name>_init()`.

### 4.2 Zero Static Mutable State in Compilation Units
All operational buffers, cache structures, bounce buffers, and tracking tables MUST reside within the Active Object instance structure (`ao_<name>_t`), which is instantiated in `app_container_t` at `main.c`. **Zero static mutable variables in `ao_<name>.c`**.

### 4.3 Dependency Injection via Pure Interfaces (DIP)
Active Objects reside in the **Application Layer**. They must NEVER include concrete adapter headers, hardware drivers, or HAL registers:
- Depend on interfaces in `include/interfaces/i_<name>.h`.
- Receive bound port handles (`i_<name>_t`) during `Init()`.
- Use validated inline wrappers (`i_<name>_<op>()`) rather than manipulating vtables directly.

### 4.4 Deterministic Testability (`ao_<name>_step`)
To enable sub-millisecond Test-Driven Development (TDD) on Host (`native_sim`), every Active Object MUST decouple its processing logic into a synchronous step/dispatch function:
```c
/* Publicly exposed for unit tests: executes one deterministic step without real-time delays */
int ao_<name>_step(ao_<name>_t *self, uint32_t elapsed_ms);
```
Unit tests instantiate the AO with in-memory fakes and exercise state transitions deterministically without sleeping real time.

### 4.5 Clean Zbus Decoupling
Active Objects communicate with other subsystems primarily through **Zephyr Zbus**:
- External changes are ingested via Zbus channel listeners and converted to AO queue events.
- State transitions produced by the AO are published to Zbus (`zbus_chan_pub`).
- The Active Object core logic must remain functional in isolated host builds where Zbus may be stubbed or replaced by callbacks.

---

## 5. Canonical Implementation Reference Templates

### 5.1 Pattern A Template Header (`ao_example_polling.h`)

```c
#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_EXAMPLE_POLLING_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_EXAMPLE_POLLING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include "interfaces/i_sensor_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AO_EXAMPLE_STACK_SIZE     2048
#define AO_EXAMPLE_PRIORITY       7
#define AO_EXAMPLE_POLL_PERIOD_MS 20U

typedef struct {
    const i_sensor_matrix_t *sensor;      /**< Injected abstract interface */
    uint32_t                 poll_period_ms;
    k_thread_stack_t        *stack;
    size_t                   stack_size;
} ao_example_config_t;

typedef struct {
    const i_sensor_matrix_t *sensor;
    uint32_t                 poll_period_ms;
    
    /* Concurrency and Lifecycle */
    struct k_thread          thread;
    k_thread_stack_t        *stack;
    size_t                   stack_size;
    struct k_sem             run_sem;     /**< Outer gate semaphore */
    struct k_sem             stopped_sem; /**< Stop confirmation semaphore */
    volatile bool            is_running;  /**< Inner loop guard */
    volatile bool            terminate;   /**< Outer loop exit flag */
    bool                     is_initialized;
} ao_example_t;

/* Standard Lifecycle API */
int  ao_example_init(ao_example_t *self, const ao_example_config_t *config);
int  ao_example_start(ao_example_t *self);
int  ao_example_stop(ao_example_t *self);
int  ao_example_wait_stopped(ao_example_t *self, k_timeout_t timeout);
int  ao_example_deinit(ao_example_t *self);

/* Deterministic step for TDD */
int  ao_example_step(ao_example_t *self, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_EXAMPLE_POLLING_H_ */
```

### 5.2 Pattern A Template Source (`ao_example_polling.c`)

```c
#include "ao_example_polling.h"
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_example, LOG_LEVEL_INF);

static void ao_example_thread_entry(void *p1, void *p2, void *p3)
{
    ao_example_t *self = (ao_example_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!self->terminate) {
        /* 1. Blocked on outer gate until ao_example_start() */
        k_sem_take(&self->run_sem, K_FOREVER);
        if (self->terminate) {
            break;
        }

        LOG_INF("ao_example thread entered inner loop");

        /* 2. Inner work loop */
        while (self->is_running) {
            (void)ao_example_step(self, self->poll_period_ms);
            k_msleep(self->poll_period_ms);
        }

        LOG_INF("ao_example thread exited inner loop");
        (void)k_sem_give(&self->stopped_sem); /* Signal wait_stopped */
    }

    (void)k_sem_give(&self->stopped_sem);     /* Signal deinit */
}

int ao_example_init(ao_example_t *self, const ao_example_config_t *config)
{
    if (self == NULL || config == NULL || config->sensor == NULL ||
        config->stack == NULL || config->stack_size == 0U) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->sensor = config->sensor;
    self->poll_period_ms = (config->poll_period_ms > 0U) ? config->poll_period_ms : AO_EXAMPLE_POLL_PERIOD_MS;
    self->stack = config->stack;
    self->stack_size = config->stack_size;

    k_sem_init(&self->run_sem, 0, 1);
    k_sem_init(&self->stopped_sem, 0, 1);

    self->is_initialized = true;

    /* Spawn thread once in gated state */
    k_thread_create(&self->thread, self->stack, self->stack_size,
                    ao_example_thread_entry, self, NULL, NULL,
                    K_PRIO_PREEMPT(AO_EXAMPLE_PRIORITY), 0, K_NO_WAIT);
    k_thread_name_set(&self->thread, "ao_example");

    return 0;
}

int ao_example_start(ao_example_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EALREADY;
    }

    self->is_running = true;
    k_sem_give(&self->run_sem);
    return 0;
}

int ao_example_stop(ao_example_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;
    return 0;
}

int ao_example_wait_stopped(ao_example_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_example_deinit(ao_example_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY; /* Must call stop() and wait_stopped() first */
    }

    self->terminate = true;
    k_sem_give(&self->run_sem); /* Awaken outer loop to exit */

    int err = k_sem_take(&self->stopped_sem, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_example thread deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}

int ao_example_step(ao_example_t *self, uint32_t elapsed_ms)
{
    if (self == NULL || self->sensor == NULL) {
        return -EINVAL;
    }
    /* Perform step logic */
    return 0;
}
```

---

## 6. Subsystem Active Object Inventory & Migration Plan

| Active Object | Assigned Pattern | Current Status | Key Refactoring Directives |
|---|---|---|---|
| [`ao_digital_input`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/src/application/active_objects/ao_digital_input.h) | **Pattern A (Polling)** | Legacy Dynamic Spawn | - Add `run_sem` & `stopped_sem`.<br>- Move `k_thread_create` to `init()`.<br>- Remove dynamic thread abort on stop. |
| [`ao_storage`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/src/application/active_objects/ao_storage.h) | **Pattern B (Message-Driven)** | Legacy Static Buffer | - Eliminate file-scope `s_storage_bounce_buffer`. Embed in struct.<br>- Add `stopped_sem` & outer queue gate.<br>- Support `AO_MSG_START`, `STOP`, `TERMINATE`. |
| [`ao_system_indicator`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/src/application/active_objects/ao_system_indicator.h) | **Pattern B (Message + Tick)** | Legacy Join/Abort | - Implement queue gate with 25 ms tick timeout.<br>- Eliminate `k_thread_abort` in stop.<br>- Add `wait_stopped()` & `deinit()`. |
| [`ao_network`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/src/application/active_objects/ao_network.h) | **Pattern B (Message + Tick)** | Legacy Join/Abort | - Implement queue gate with 500 ms status poll tick.<br>- Remove thread recreation on start.<br>- Add `stopped_sem` synchronization. |
| [`ao_modbus`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/src/application/active_objects/ao_modbus.h) | **Pattern C (Scheduled Arbiter)** | Legacy Join/Wait | - Unify with standard lifecycle (`wait_stopped`, `deinit`).<br>- Retain dual-bus isolation (one AO instance per RS-485 bus). |

---

## 7. Composition Root Orchestration Protocol

The system startup and shutdown sequencing in `composition_root.c` follows this deterministic order:

```c
/* 1. INITIALIZATION: composition_root_init(&g_app) */
/* All AOs are initialized. Threads are spawned and immediately blocked on gates. */
ao_digital_input_init(&self->ao_di, &di_cfg);
ao_storage_init(&self->ao_store, &storage_cfg);
ao_network_init(&self->ao_net, &net_cfg);
ao_system_indicator_init(&self->ao_indicator, &indicator_cfg);
for (int i = 0; i < MODBUS_BUS_MAX; i++) {
    ao_modbus_init(&self->ao[i], &modbus_cfg[i]);
}

/* 2. START: composition_root_start(&g_app) */
/* Gates are opened. Threads enter their inner execution loops. */
ao_storage_start(&self->ao_store);
ao_network_start(&self->ao_net);
ao_system_indicator_start(&self->ao_indicator);
ao_digital_input_start(&self->ao_di);
for (int i = 0; i < MODBUS_BUS_MAX; i++) {
    ao_modbus_start(&self->ao[i]);
}

/* 3. SHUTDOWN: composition_root_stop(&g_app) */
/* Graceful, coordinated stop without thread destruction. */
ao_digital_input_stop(&self->ao_di);
ao_digital_input_wait_stopped(&self->ao_di, K_MSEC(500));

for (int i = 0; i < MODBUS_BUS_MAX; i++) {
    ao_modbus_stop(&self->ao[i]);
    ao_modbus_wait_stopped(&self->ao[i], K_MSEC(1000));
}

ao_network_stop(&self->ao_net);
ao_network_wait_stopped(&self->ao_net, K_MSEC(1000));

ao_storage_stop(&self->ao_store);
ao_storage_wait_stopped(&self->ao_store, K_MSEC(2000));

ao_system_indicator_stop(&self->ao_indicator);
ao_system_indicator_wait_stopped(&self->ao_indicator, K_MSEC(500));
```
