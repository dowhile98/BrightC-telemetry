# Firmware Audit — Second Pass (`35be7fe`)

- **Date:** 2026-11-06
- **Repository:** `BrightC-telemetry` (Zephyr RTOS, STM32H743ZIT6)
- **Baseline commit:** `35be7fe` — *docs: Update subsystem progress logs for P2-1 through P2-11 audit resolution*
- **Scope:** static review of `app/src`, `app/include`, `tests/`, `doc/`. No source modifications, no build, no flash.
- **Supersedes:** the previous audit report (that tree predates `app_facade`, `shell_brightc`, `config_envelope`, `default_config_profiles.c`).
- **Reference specification:** [`doc/request/Mathematical_Model_Specification_REVF.md`](../request/Mathematical_Model_Specification_REVF.md) (Revision F).

## Severity rubric

| Level | Meaning |
| :-- | :-- |
| **P0** | Blocking: a documented product function does not work or silently destroys data. |
| **P1** | High: architectural invariant violated, or a feature reports success while doing nothing. |
| **P2** | Medium: correctness/robustness risk with limited blast radius, or technical debt with a direct cost. |
| **P3** | Low: hygiene, documentation drift, test gaps. |

---

## 1. Executive summary

The **structure** of the firmware improved substantially since the previous audit: the configuration surface now exists (shell + application facade), the EEPROM envelope has magic/version/CRC, compile-time size assertions exist, degraded storage boot is implemented, and profile switching actually reaches the actuator.

The problem has moved to the **data plane**. Three functional chains terminate in the void:

1. **Temperature is read from the bus and discarded.** The HVAC control loop therefore evaluates with `highest_temp = 0.0` and never enters `COOLING`; the shell reports `0.0 °C` for all 21 channels.
2. **Relay writes are emitted without change detection.** At 8 units × 3 relays × 1 Hz with a 40 ms turnaround delay, the Modbus thread is saturated and commands are dropped from an 8-deep queue.
3. **Four configuration domains persist compile-time constants**, not user data, and are never loaded at boot.

Per Revision F §1, the product is primarily a *telemetry, energy auditing and predictive analytics* unit. Finding **P0-01** therefore hits the core deliverable, not a secondary feature.

---

## 2. Resolved since the previous audit (verified)

| Previous ID | Finding | Current evidence |
| :-- | :-- | :-- |
| P0-2 | Runtime profile switch did not reach the actuator | `ao_hvac.c:469`, `composition_root.c:1479` call `adapter_hvac_actuator_set_hw_config` |
| P0-3 | External Modbus digital inputs returned `-ENOTSUP` | `ao_modbus_set_mbsl32di_callback` (`composition_root.c:977`) → `on_modbus_mbsl32di_reading` (`:879`) → `adapter_di_gpio_set_modbus_raw` (`:888`) |
| P0-4 | Persisted bus config never reached the UART | `app_facade_modbus_configure_bus` calls `i_modbus_client_set_bus_cfg` (`app_facade.c:486`) |
| P0-5 | Modbus runtime changes not persisted | `ao_storage_mark_dirty(... CONFIG_DOMAIN_MODBUS ...)` in `app_facade.c:458` and `:486` |
| P1-4 | Profile tables as `static const` in a header | Moved to `app/src/application/config/default_config_profiles.c` (553 lines); Kconfig overrides in `app/include/common/default_config_profiles.h` |
| P1-6 | No schema versioning | `app/include/common/config_envelope.h` — magic `BCTL`, version, domain, payload length, CRC-16-CCITT |
| P1-7 | `storage_api_load` silently truncated | Returns `-ENOBUFS` |
| P1-8 | No compile-time size checks | `BUILD_ASSERT` × 10 in `composition_root.c:20-42` (payload fit + domain overlap) |
| P1-9 | No factory reset | Button hold 5 s (`main.c`) + `storage reset` shell command |
| P1-10 | Boot aborted without EEPROM | `ao_storage_init` sets `is_degraded` and keeps running |
| P1-12 | Unknown relay backend returned `0` | Returns `-EINVAL` (`adapter_hvac_actuator.c`) |
| P1-15 | `storage_subsystem.md` described non-existent behaviour | Rewritten |

New capabilities delivered: `app/src/presentation/shell/shell_brightc.c` (553 lines), `app/src/application/services/app_facade.{h,c}` (397 + 634 lines), `config_envelope`, full DI initializer split into `composition_root_init_*`, factory reset path.

---

## 3. P0 — Blocking findings

### P0-01 · The temperature acquisition chain is broken in three places

**Impact:** the HVAC control loop has no temperature input; the shell reports zeros; every psychrometric metric of Rev F §4 is unreachable. This invalidates the core telemetry deliverable.

**Evidence**

1. **Read and discarded** — `app/src/application/active_objects/ao_modbus.c:27-33`:
   ```c
   case MODBUS_DEV_TYPE_CWT_TM16PT:
       return i_modbus_client_read_holding_registers(
           self->client, target->slave_addr, CWT_TM16PT_REG_TEMP_SCALED_BASE,
           CWT_TM16PT_SCALED_REG_COUNT, registers, (int32_t)target->timeout_ms);
   ```
   No parse, no callback, no publish. Compare with the `MBSL32DI` and `JSY_MK339` branches, which do parse.

2. **Parsers exist and are never called** — `cwt_tm16pt_parse_scaled_temperatures()` and `cwt_tm16pt_parse_float_temperatures()` (`app/src/domain/services/cwt_tm16pt_codec.h:44,56`) have zero callers outside their own translation unit.

3. **No consumer** — `ao_hvac_feed_temperatures()` (`ao_hvac.c:331`) and `ao_hvac_set_group_metrics()` (`ao_hvac.c:316`) have **zero callers** in firmware. `group_metrics[]` is only written inside those dead functions.

4. **Facade cache never populated** — `s_sensor_cache` is `memset` in `app_facade_init` (`app_facade.c:117-122`) and only ever read afterwards (`app_facade.c:329-345`). `sensor status` therefore prints `0.0 °C` / `OPEN / INVALID` permanently.

**Consequence chain**

```
CWT-TM-16PT read → discarded
      ↓
cwt_tm16pt_parse_* never called
      ↓
ao_hvac_feed_temperatures never called
      ↓
group_metrics[g].highest_temp = 0.0
      ↓
hvac_group_entity_evaluate(): 0 <= temp_min (~22 °C) → never COOLING
      ↓
sensor shell view: all channels invalid
```

**Required fix (single path)**

```
ao_modbus poll_target(CWT_TM16PT)
   → cwt_tm16pt_parse_scaled_temperatures()
   → temperature callback (mirror of mbsl32di_cb)
   → ao_hvac_feed_temperatures()   [+ facade sensor cache update]
```

The `mbsl32di_cb` pattern already in `ao_modbus.h:79` is the correct template: it was added for exactly this class of problem and is already wired.

**Verification after fix**

- `sensor status` shows plausible °C values for 16 external + 5 internal channels.
- `hvac status` shows a non-zero `Temp Highest`.
- A group with `temp_max` below the measured temperature enters `COOLING`.

**Spec reference:** §2.2 (CWT-TM-16PT primary acquisition path), §4.1 (`(N+N)+M+L` matrix), §6.1 (1 Hz accumulators).

---

### P0-02 · Relay writes saturate the Modbus bus and are silently dropped

**Impact:** HVAC relay commands are lost under normal operation; the Modbus polling of slave devices is starved; the relay cache reports states that were never applied.

**Evidence**

1. `ao_hvac_step()` (`ao_hvac.c:181`) runs every `AO_HVAC_PERIOD_MS = 1000` (`ao_hvac.h:41`) and, for **every active unit**, unconditionally calls:
   ```c
   hvac_tri_state_t tri = hvac_unit_entity_get_tri_state(&self->units[u]);
   (void)i_hvac_unit_set_tri_state(&self->actuator_port, u, tri);
   ```
   There is no comparison against `adapter_hvac_actuator_t.cached_tri_state[unit_id]`, which already exists and is already updated.

2. Each call dispatches **three** endpoints (AA, SM, G) via `dispatch_endpoint_relay()`.

3. Remote endpoints enqueue one `AO_MODBUS_EVENT_WRITE_COIL` per relay (`adapter_relay_modbus.c:30-64`).

4. The Modbus AO sleeps `AO_MODBUS_INTER_POLL_DELAY_MS = 40` ms after **every** write (`ao_modbus.c:203`), plus ~8-10 ms of frame time at 9600 baud.

5. `AO_MODBUS_QUEUE_DEPTH = 8` (`ao_modbus.h:37`).

**Arithmetic (control-only profile, 8 units)**

| Quantity | Value |
| :-- | :-- |
| Relay writes per second | 8 units × 3 relays × 1 Hz = **24** |
| Turnaround delay alone | 24 × 40 ms = **960 ms/s** |
| Frame time at 9600 8N1 | ~8.3 ms → +**200 ms/s** |
| Result | **>100 % of the bus** |
| Queue depth | 8 |

When the queue fills, `ao_modbus_post` returns `-ENOMSG` and the command is lost. Worse, `relay_modbus_set_state` updates `self->cache[channel]` **before** posting ("optimistically"), so `get_state` reports the intended state, not the applied one.

**Fix (three lines)**

```c
/* adapter_hvac_actuator.c — adapter_hvac_actuator_set_tri_state() */
if (self->cached_tri_state[unit_id] == tri) {
    return 0;
}
```

This is sufficient because the cached state is already maintained and profile changes reseed `hw_config`. Optionally also compare in `ao_hvac_step` to avoid crossing the port at all.

**Secondary hardening (recommended)**

- Do not update the relay cache before the write is acknowledged; reconcile from the FC01 poll instead.
- Consider raising the queue depth only after the redundant-write fix; depth hides the symptom, not the cause.

---

### P0-03 · Four configuration domains persist compile-time constants

**Impact:** `DEVICE`, `CALIBRATION`, `SENSOR_CFG` and `BASELINE` appear persistent but can never hold user data. Calibration is impossible today, and `storage save` would overwrite the domain with the compiled default.

**Evidence**

```c
/* composition_root.c:138-180 */
const config_device_t      *cfg = default_config_get_device_cfg();
const config_calibration_t *cfg = default_config_get_calibration_cfg();
const config_sensor_cfg_t  *cfg = default_config_get_sensor_cfg();
const config_baseline_t    *cfg = default_config_get_baseline_cfg();
return config_envelope_pack(CONFIG_DOMAIN_*, cfg, sizeof(*cfg), ...);
```

- No RAM state exists for these domains.
- No boot-time load: only `MODBUS` (`:278`), `NET_ETH` (`:523`), `NET_WIFI` (`:559`) and `HVAC` (`:775`) are loaded.
- `app_facade_sensor_set_calibration()` (`app_facade.c:349`) writes `s_sensor_cal_offsets[]` — a private facade array — and never marks `CONFIG_DOMAIN_CALIBRATION` dirty.

**Consequence:** `test_all_config_domains_registered_and_persistable` passes because it only asserts that a provider is registered. It gives false confidence.

**Note vs. spec:** Rev F §6.2 explicitly requires the EEPROM to hold the OLS coefficients `β0..β3` (64-bit `double`, 32 bytes), `η_II,base`, `ΔT_base,k`, `T_inj_base,k` and `ΔP_comp,c`. The `BASELINE` domain is therefore a **specified future requirement**, not an accidental stub. The defect is claiming persistence today, not the absence of the feature.

**Required action (choose one, explicitly)**

- **Option A:** give all four domains real RAM state, load them at boot, and expose mutation APIs; document which are deferred.
- **Option B:** remove the four providers from the registry until implemented, and mark them `Reserved / not implemented` in `doc/architecture/storage_subsystem.md` and `doc/progress/`.

Option B is honest short-term; Option A is the target. Mixing the two (current state) is the only unacceptable outcome.

---

## 4. P1 — High findings

### P1-01 · Two dependency-rule violations (application → infrastructure)

```
app/src/application/services/app_facade.c:18    #include "infrastructure/di/composition_root.h"
app/src/application/active_objects/ao_hvac.c:10 #include "infrastructure/adapters/adapter_hvac_actuator.h"
```

Plus a concrete type inside an application struct:

```c
/* ao_hvac.h:72 */
struct adapter_hvac_actuator_s *actuator_adapter;
```

AGENTS.md §4.1.D and Invariant 5.2 require high-level modules to depend only on ports. The domain layer remains clean (0 Zephyr/include-of-infrastructure), but the application layer now reaches downward.

**Root cause:** `app_facade` is a *presentation adapter* placed in `application/services/`. It translates shell calls into `app_container_*` calls; it is infrastructure by nature.

**Recommended refactor**

- Declare `app/include/interfaces/i_app_facade.h` (pure contract, no Zephyr).
- Move the implementation to `app/src/infrastructure/adapters/adapter_app_facade.c`.
- Replace `adapter_hvac_actuator_t *` in `ao_hvac` with the existing `i_hvac_unit_t *` port (already stored as `actuator_port`), and use a dedicated `i_hvac_actuator_config_t`-style port method for `set_hw_config` if needed.

### P1-02 · The facade reintroduces mutable file-scope state

`main.c` states: *"The only mutable static object of the firmware is the container below."* The facade adds four:

```c
static app_container_t *s_app = NULL;          /* app_facade.c:25 */
static storage_stats_t  s_storage_stats = {0}; /* :28 */
static int16_t s_sensor_cal_offsets[21] = {0}; /* :31 */
static struct { ... } s_sensor_cache;          /* :35 */
```

This violates **Invariant 6 (Zero File-Scope Mutable State)**.

Additionally `app_facade_init(void *container)` takes an untyped pointer, stores it in a singleton, and its return value is discarded in `main.c`. Consequences: no multi-instance support, no isolation in tests, no compile-time type checking.

**Recommended refactor:** introduce `app_facade_t` (owning `const app_container_t *` and the caches) and pass it explicitly, or keep the singleton but move it into `app_container_t` as a `facade` sub-struct.

### P1-03 · `device_id` is validated but not multiplexed

`dispatch_endpoint_relay()` was improved (unknown backend → `-EINVAL`, out-of-range `device_id` → `-ENODEV`), but the routing is still single-endpoint:

```c
if (endpoint->device_id != 0U && endpoint->device_id != DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY)
    return -ENODEV;                       /* accepts exactly one ID */
return i_relay_set_state(&self->remote_relay_port, ...);   /* exactly one port */
```

- One `remote_relay_port`, one `local_relay_port`.
- `HVAC_IO_BACKEND_LOCAL_MCP23S17` falls through the same branch as `HVAC_IO_BACKEND_LOCAL_GPIO` and reaches the same adapter instance (`composition_root.c:824`), so the enum distinction is cosmetic.

This validates the field to appear correct without implementing the routing.

**Decision (see §6, D-01): multiplexing is required.** The contract must change before field installations exist:
- Either `i_relay_t` gains a `device_id` selector, or
- The actuator holds an array of `i_relay_t` indexed by `device_id`, resolved at init.

### P1-04 · `storage_reload_all` reloads only HVAC, and factory reset is inconsistent

```c
int app_facade_storage_reload_all(void)   /* app_facade.c:544 */
{
    /* Re-read HVAC configuration */      /* only HVAC */
    ...
    return 0;                             /* always 0, errors swallowed */
}
```

- Modbus and network are not reloaded.
- Returns `0` even when `i_config_storage_load` or `config_envelope_unpack` fails.

`app_facade_storage_reset_defaults()` (`:567`) writes defaults to EEPROM and reapplies HVAC + DI in RAM, but Modbus and network keep running with the pre-reset configuration until reboot. The shell message *"EEPROM reset to factory defaults and default profile reapplied"* is misleading.

**Recommended fix:** either reboot automatically after a factory reset, or implement a real 8-domain reload and reapply each subsystem.

### P1-05 · Uninitialized stack read in `reload_all`

```c
config_hvac_t hvac_cfg;                                  /* uninitialized */
config_envelope_unpack(CONFIG_DOMAIN_HVAC, buf, loaded_size,
                       &hvac_cfg, sizeof(hvac_cfg), NULL); /* out_len == NULL */
```

Passing `NULL` for `out_payload_len` means the exact payload length is never checked against `sizeof(config_hvac_t)`. A short-but-CRC-valid payload leaves the tail of `hvac_cfg` as stack garbage, which `config_hvac_is_valid()` may still accept.

**Fix:** `memset(&hvac_cfg, 0, sizeof(hvac_cfg))` and require `out_len == sizeof(config_hvac_t)` before applying.

### P1-06 · `run_fan` is ignored and `FAN_ONLY` behaves exactly like `OFF`

- `app_facade_hvac_set_unit_manual()` declares `ARG_UNUSED(run_fan)` (`app_facade.c:298`) while the shell exposes `[fan_on|fan_off]`.
- `app_facade_hvac_set_mode(HVAC_MODE_FAN_ONLY)` executes the same code path as `HVAC_MODE_OFF` (`app_facade.c:209-231`): manual mode + `hvac_unit_entity_set_manual(false)` on every unit. The comment claims *"Run fan purge"*.

**Impact:** the operator believes purge ventilation is active; no relay changes.

### P1-07 · `set_setpoint` overwrites configured thresholds and accepts inverted setpoints

```c
group_cfg.temp_alarm  = cool_target + 3.0f;   /* hardcoded */
group_cfg.temp_bypass = cool_target + 6.0f;   /* hardcoded */
```

- Destroys the alarm/bypass margins configured in the plant profile.
- No check that `heat_target_x10 < cool_target_x10`; an inverted pair yields `temp_min > temp_max`.

### P1-08 · Two "mode" enums with numeric collision

| `app_facade.h` | `hvac_types.h` |
| :-- | :-- |
| `HVAC_MODE_OFF = 0` | `HVAC_MODE_TELEMETRY_ONLY = 0` |
| `HVAC_MODE_COOL = 1` | `HVAC_MODE_HVAC_CONTROL_ONLY = 1` |
| `HVAC_MODE_HEAT = 2` | `HVAC_MODE_HYBRID_FULL = 2` |
| `HVAC_MODE_AUTO = 3` | — |
| `HVAC_MODE_FAN_ONLY = 4` | — |
| `HVAC_MODE_EMERGENCY = 5` | — |

No name collision, but `HVAC_MODE_HEAT == HVAC_MODE_HYBRID_FULL == 2`. Any cast between the two enums compiles and silently selects the wrong plant profile.

**Fix:** rename to `HVAC_GROUP_MODE_*` and `HVAC_PLANT_PROFILE_*`.

### P1-09 · Zbus has zero subscribers

- Channels defined: 6 (`config`, `digital_input`, `hvac`, `indicator` ×3, `network` ×2).
- `ZBUS_OBSERVERS_EMPTY` in every definition (`config_channels.c:15` and peers).
- `ZBUS_LISTENER_DEFINE` / `zbus_sub_wait`: **0** in `app/src`.
- `zbus_chan_pub`: 5 sites (`ao_digital_input`, `ao_hvac`, `ao_network` ×2, `ao_storage`).

`CONFIG_ZBUS=y` reserves channel structures and message buffers; five publications per second reach nobody. The architecture documents continue to present Zbus as the asynchronous event mechanism.

**Decision (see §6, D-06 pending):** either wire the subscribers (the facade / HMI is the natural consumer) or remove Zbus and document synchronous access.

---

## 5. P2 / P3

| ID | Finding | Location | Notes |
| :-- | :-- | :-- | :-- |
| P2-01 | `s_storage_stats` mutated from shell and flush without protection; `crc_errors` is never incremented | `app_facade.c:28,525,584` | Counters shown by `storage stats` are partly fictional |
| P2-02 | `free_heap_bytes = 0` hardcoded; `firmware_version = "v3.0.0"` in source | `app_facade.c:597-626` | `sys info` prints a heap that does not exist |
| P2-03 | `CONFIG_MAIN_STACK_SIZE` duplicated; heap 32 KiB + malloc arena 16 KiB contradict Invariant 5; `THREAD_ANALYZER` and `DEBUG_THREAD_INFO` in production config | `app/prj.conf:6-8,27` | 48 KiB reserved for allocators that the code never uses |
| P2-04 | `CONFIG_SHELL=y` unconditional | `app/prj.conf` | See D-04: shell stays in production; authentication is a planned feature, not a defect |
| P2-05 | `composition_root.c` still a God file: 1542 lines mixing wiring and policy (defaults, fallback, providers) | `composition_root.c` | Violates the SRP stated in AGENTS.md §4.1 |
| P2-06 | `thermodynamic_engine` (736 lines) and `sensor_matrix_mapper` (400 lines) have no firmware callers; only tests exercise them | `app/src/domain/services/` | See D-02: specified future telemetry, must not be deleted |
| P2-07 | Ports declared with no implementer: `i_energy_meter`, `i_storage_fs`, `i_sensor_matrix` | `app/include/interfaces/` | Planned per Rev F §2.4, §6.3 |
| P2-08 | JSY-MK-339 readings parsed then only `LOG_DBG`; no retention, no publication, no facade view | `ao_modbus.c:34-56` | See D-03: energy feeds control and telemetry |
| P2-09 | Waveshare relay states parsed and discarded with a `TODO`; optimistic cache never reconciled | `ao_modbus.c:90-100` | Interacts with P0-02 |
| P2-10 | Digital input scan period 20 ms × 34 channels over SPI | `ao_digital_input.h:39` | **Mandated** by Rev F §2.3 (50 Hz sampling, 500 ms / 25-sample debounce). Not a defect; monitor SPI utilisation |
| P2-11 | `app_facade_init(void *)` untyped and return value discarded; boot blocks up to 5 s if the reset button is stuck | `main.c`, `app_facade.c:110` | |
| P3-01 | No tests for `app_facade`, `shell_brightc`, `config_envelope`, `default_config_profiles`, hot profile switching, or the factory-reset path | `tests/` | `config_envelope` is pure logic and the cheapest high-value suite to add |
| P3-02 | `doc/progress` reports HVAC 100 %, Sensors 85 %, Storage 95 % with P0-01/02/03 open | `doc/progress/README.md` | Percentages should be reconciled with this document |

---

## 6. Decisions recorded (from the 2026-11-06 review)

| ID | Topic | Decision |
| :-- | :-- | :-- |
| **D-01** | `hvac_io_endpoint_t.device_id` multiplexing | **Yes, real multiplexing is required.** The endpoint must select among multiple local expanders and multiple remote relay modules. The single-port contract is insufficient and must change (P1-03). |
| **D-02** | Thermodynamic / psychrometric engine | **It is part of the telemetry function.** Behaviour is defined by [`Mathematical_Model_Specification_REVF.md`](../request/Mathematical_Model_Specification_REVF.md), §4 (thermodynamic and psychrometric engine) and §5 (predictive analytics). Implementation is **deliberately paused**; the code must be kept, not deleted or flagged as dead. Do not schedule it as a bug fix. |
| **D-03** | Energy metering (`JSY-MK-339`) | **Used by both HVAC control and telemetry.** Spec §2.4 (metering), §4.5 (`COP_real = Q_total / P_elec_total`), §5.1 (expected COP), §5.4 (`Q_max_safe`, `kW_free`). Telemetry consumers are not implemented yet, so the work is deferred — but the reading must at minimum be **retained in RAM and exposed**, not only logged (P2-08). |
| **D-04** | Shell in production | **Yes, the shell must exist in production.** Authentication is a planned feature (scheme and threat model to be designed before release). P2-04 is therefore a roadmap item, not a defect. |
| **D-05** | Factory reset trigger | Button reset exists today. Additional triggers (**Modbus, SNMP, web server**) will be implemented later. Until then, P1-04 (reset leaves Modbus/network on the old configuration until reboot) still applies to the button path. |
| **D-06** | Zbus | Open. Either wire subscribers or remove the dependency. |

---

## 7. Recommended order of work

**Immediate**

1. **P0-01** — wire `CWT parse → callback → ao_hvac_feed_temperatures → facade cache`. One path restores the core telemetry function and the HVAC control loop.
2. **P0-02** — add the three-line change-detection guard in `adapter_hvac_actuator_set_tri_state`.
3. **P0-03** — decide Option A or B and make the domain list match reality.

**Short term**

4. P1-04 / P1-05 — full 8-domain reload and reboot-after-reset; `memset` + exact length check.
5. P1-06 / P1-07 — real `run_fan`, real `FAN_ONLY`, `heat < cool` validation, preserve alarm/bypass margins.
6. P1-02 — remove the facade statics (Invariant 6).
7. P1-03 — design the `device_id` multiplexing contract (D-01) before any field installation.

**Structural**

8. P1-01 — move the facade to `infrastructure/adapters/` behind `i_app_facade.h`; replace the concrete actuator pointer in `ao_hvac` with a port.
9. P1-09 / D-06 — decide on Zbus.
10. P3-01 — add `config_envelope` and hot-profile-switch test suites first.

**Deferred by decision (do not schedule as defects)**

- D-02: psychrometric / thermodynamic engine integration.
- D-03: telemetry consumers of energy data (store & forward, Modbus TCP, SNMP, REST).
- D-04: shell authentication.
- D-05: factory reset via Modbus / SNMP / web.

---

## 8. How to reproduce the audit

```bash
cd BrightC-telemetry

# P0-01 — CWT read without parse; no consumers
grep -n "CWT_TM16PT" -A 6 app/src/application/active_objects/ao_modbus.c
grep -rn "cwt_tm16pt_parse" app/src | grep -v domain/services
grep -rn "ao_hvac_feed_temperatures\|ao_hvac_set_group_metrics" app/src | grep -v ao_hvac.c

# P0-02 — no change detection before actuation
sed -n '181,235p' app/src/application/active_objects/ao_hvac.c
grep -n "cached_tri_state" app/src/infrastructure/adapters/adapter_hvac_actuator.c

# P0-03 — providers return compile-time constants
sed -n '138,182p' app/src/infrastructure/di/composition_root.c
grep -n "i_config_storage_load" app/src/infrastructure/di/composition_root.c

# P1-01 — dependency rule
grep -rn '#include "infrastructure' app/src/application app/src/presentation
grep -rn "<zephyr/" app/src/domain app/include/common app/include/interfaces   # expect 0

# P1-02 — mutable file-scope state
grep -n "^static [a-zA-Z_].*[;=]" app/src/application/services/app_facade.c

# P1-09 — Zbus without observers
grep -rn "ZBUS_OBSERVERS_EMPTY" app/src
grep -rn "ZBUS_LISTENER_DEFINE\|zbus_sub_wait" app/src        # expect 0
```

---

## 9. Traceability

| This document | Related artefacts |
| :-- | :-- |
| P0-01, P1-03 | `doc/architecture/sensor_subsystem.md`, `doc/architecture/hvac_control_subsystem.md` |
| P0-03, P1-04, P1-05 | `doc/architecture/storage_subsystem.md`, `doc/progress/storage_progress.md` |
| P1-01, P1-02 | `AGENTS.md` §4.1.D, Invariants 5 and 6 |
| P1-09 | `doc/architecture/active_object_architecture.md`, `doc/architecture/network_subsystem.md` |
| D-02 | `doc/request/Mathematical_Model_Specification_REVF.md` §4, §5 |
| D-03 | `doc/request/Mathematical_Model_Specification_REVF.md` §2.4, §4.5, §5.1, §5.4 |
| D-04, D-05 | `app/prj.conf`, `main.c`, `doc/progress/core_architecture_progress.md` |
