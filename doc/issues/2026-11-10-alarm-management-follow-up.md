# Alarm Management Follow-up Issues

**Date:** 2026-11-10  
**Scope:** `app/src/application/active_objects/ao_alarm_manager.c`, alarm detectors, Zbus channels, and composition-root wiring  
**Status:** Open backlog for future correction  
**Review basis:** Post-change static review of commit `8f714d5`

## Summary

The alarm implementation has incorporated the main domain and detector changes from the previous review. The issues below remain in the integration and robustness layers. No source-code changes are included in this document.

| ID | Area | Finding | Priority | Status |
|---|---|---|---|---|
| ALARM-FU-001 | Zbus | Alarm transition listeners are not attached to the transition channel | P0 | Open |
| ALARM-FU-002 | Network | Ethernet alarm listener is not attached to the Ethernet state channel | P1 | Open |
| ALARM-FU-003 | Storage | MicroSD fault alarm has no producer | P1 | Open |
| ALARM-FU-004 | Concurrency | Dynamic descriptor registration races with registry readers | P1 | Open |
| ALARM-FU-005 | API validation | Source, acknowledge, and latch commands accept invalid targets | P2 | Open |
| ALARM-FU-006 | Query semantics | Active-duration API disagrees with the snapshot contract after clear | P2 | Open |
| ALARM-FU-007 | Architecture | Mutable file-scope container pointer violates the state invariant | P1 | Open |
| ALARM-FU-008 | Capacity | Default catalog truncation is silent when Kconfig capacity is too small | P2 | Open |
| ALARM-FU-009 | Integration | Required external alarm consumers are not wired | P1 | Open |
| ALARM-FU-010 | Lifecycle | STOP delivery depends on free message-queue capacity | P2 | Open |
| ALARM-FU-011 | Identifier model | Generic HVAC bypass identifier aliases Group 1 | P2 | Open |

## ALARM-FU-001 — Transition channel has no observers

**Priority:** P0  
**Evidence:** `app/src/application/zbus/alarm_channels.c:11-16` defines `chan_alarm_transition` with `ZBUS_OBSERVERS_EMPTY`.  
The composition root defines `lis_alarm_logger`, but the channel does not subscribe it.

**Impact:** Transition events are published by the alarm manager but no registered Zbus listener receives them. Logging and any future event-driven consumer connected through this channel remain inactive.

**Requirements affected:** FR-26 and the alarm architecture's transition distribution contract.

**Acceptance criteria:**

- The intended transition consumers are listed in the channel's observer set.
- A test publishes an activation and clear event and verifies listener delivery.
- Listener delivery remains asynchronous and does not block the alarm manager.

## ALARM-FU-002 — Ethernet alarm listener is not subscribed

**Priority:** P1  
**Evidence:** `app/src/application/zbus/network_channels.c:10-16` defines `chan_network_eth_state` with `ZBUS_OBSERVERS_EMPTY`.  
`on_alarm_net_eth_feed_cb()` exists in `composition_root.c`, but it cannot be invoked through this channel.

**Impact:** `ALARM_ID_INFRA_ETHERNET_DOWN` does not follow the actual Ethernet link state.

**Acceptance criteria:**

- Ethernet state transitions activate and clear the infrastructure alarm.
- The callback is subscribed only to the Ethernet channel.
- A native-sim test covers disconnected, connected, and repeated-state cases.

## ALARM-FU-003 — MicroSD alarm has no producer

**Priority:** P1  
**Evidence:** `ALARM_ID_INFRA_SD_CARD_FAULT` exists in the catalog, but no producer was found for card-detect changes, mount failures, or filesystem write failures.

**Impact:** The registered alarm can never represent the real SD-card health state.

**Requirements affected:** FR-21.

**Acceptance criteria:**

- Card removal asserts `ALARM_ID_INFRA_SD_CARD_FAULT`.
- Mount and write failures assert the same alarm.
- Card reinsertion and successful recovery clear the alarm.
- Detection is routed through a port/adapter boundary and is unit-testable without hardware.

## ALARM-FU-004 — Dynamic registry access is not consistently synchronized

**Priority:** P1  
**Evidence:** `ao_alarm_manager_register_descriptor()` mutates the registry under `self->lock`, while the worker and query paths call `alarm_registry_find_index()` and read descriptor arrays outside that lock.

**Impact:** Registering a descriptor while the manager is running can race with event dispatch or queries. The resulting index or descriptor access can be inconsistent.

**Acceptance criteria:**

- Either registration is rejected after `ao_alarm_manager_start()`, or all registry reads and writes use one synchronization policy.
- Concurrent registration/query/dispatch tests complete without inconsistent indices.
- Registration failure semantics are documented for the chosen lifecycle policy.

## ALARM-FU-005 — Command APIs do not validate target capabilities

**Priority:** P2  
**Evidence:**

- `ao_alarm_manager_post_source_condition()` validates existence but not `desc->is_aggregated`.
- `ao_alarm_manager_post_ack()` and `ao_alarm_manager_post_reset_latch()` do not validate existence or descriptor capability.

**Impact:** Invalid commands are accepted into the queue and silently ignored by the worker, hiding caller errors.

**Acceptance criteria:**

- Source updates for non-aggregated alarms return `-EINVAL` or `-ENOTSUP`.
- Acknowledge requests reject alarms without acknowledgement support.
- Latch reset requests reject alarms without latch support.
- Unknown IDs return `-ENOENT` consistently.

## ALARM-FU-006 — Active-duration query has inconsistent post-clear semantics

**Priority:** P2  
**Evidence:** The worker stores the completed duration in `snapshot[idx].active_duration_ms` on clear, but `ao_alarm_manager_get_active_duration()` returns zero for an inactive alarm.

**Impact:** Callers using the query port cannot retrieve the completed duration even though the snapshot contains it. This creates two different interpretations of the same field.

**Acceptance criteria:**

- The API contract explicitly defines active and cleared behavior.
- The implementation returns live duration while active and the final stored duration after clear, or documents and consistently enforces another policy.
- Tests cover active, acknowledged, latched, cleared, and reactivated states.

## ALARM-FU-007 — Mutable global container reference violates architecture invariant

**Priority:** P1  
**Evidence:** `composition_root.c:1154` defines `static app_container_t *s_active_container` and updates it during initialization/deinitialization.

**Impact:** This violates the project's zero file-scope mutable state invariant and makes callback ownership dependent on global mutable state.

**Acceptance criteria:**

- No mutable file-scope application pointer remains outside the explicitly allowed `g_app` in `main.c`.
- Zbus callback context is passed through an instance-owned mechanism or an equivalent approved composition-root design.
- Static audit confirms no additional mutable file-scope state.

## ALARM-FU-008 — Alarm catalog truncation is silent

**Priority:** P2  
**Evidence:** `alarm_registry_init()` limits `default_count` to `CONFIG_ALARM_MANAGER_MAX_ALARMS` without reporting an error.

**Impact:** A configuration below the catalog size silently removes alarms from the runtime registry. Producers then receive `-ENOENT`, while the firmware appears initialized.

**Acceptance criteria:**

- Catalog capacity is checked at build time or initialization fails explicitly.
- The configured minimum capacity is greater than or equal to the built-in catalog size.
- A test verifies behavior when capacity is insufficient.

## ALARM-FU-009 — External alarm consumers are not wired

**Priority:** P1  
**Evidence:** The architecture and requirements specify persistent logging, SNMP traps, HMI/Web queries, and Modbus alarm registers. The current implementation exposes the query port and defines the transition channel, but no complete consumers for those integrations were found. The existing composition-root logger only emits `LOG_INF()`.

**Impact:** Alarm state is available internally but is not yet delivered to the required operational interfaces or persistent audit trail.

**Requirements affected:** FR-26 through FR-29.

**Acceptance criteria:**

- Persistent logging records activation and clear events with timestamp, ID, event, value, and source mask.
- SNMP processing is asynchronous and subscribed to the transition channel.
- HMI/Web and Modbus server paths consume `i_alarm_query_t` or a dedicated adapter.
- Integration tests verify each consumer independently.

## ALARM-FU-010 — STOP command can be dropped when the queue is full

**Priority:** P2  
**Evidence:** `ao_alarm_manager_stop()` uses `k_msgq_put(..., K_NO_WAIT)` on the same queue used for alarm updates.

**Impact:** Under burst conditions, shutdown may fail to enqueue STOP. `deinit()` then waits and may force-abort the thread.

**Acceptance criteria:**

- Shutdown has a delivery mechanism independent of normal update capacity, or reserves queue capacity for control commands.
- Queue saturation tests verify bounded and clean shutdown.
- The stop API reports a deterministic error when shutdown cannot be guaranteed.

## ALARM-FU-011 — Generic bypass identifier aliases Group 1

**Priority:** P2  
**Evidence:** `ALARM_ID_HVAC_BYPASS_ACTIVE` and `ALARM_ID_HVAC_G1_BYPASS_ACTIVE` both equal `0x0050`.

**Impact:** External clients cannot distinguish a generic bypass alarm from Group 1 bypass by numeric ID. This creates ambiguity for persistence, telemetry, and protocol mappings.

**Acceptance criteria:**

- The generic identifier is removed or assigned a distinct documented ID.
- All protocol and catalog mappings use one unambiguous identifier model.
- Compatibility impact is documented if an existing ID must be retained.

## Recommended correction order

1. Resolve `ALARM-FU-001` and `ALARM-FU-002` because they disable existing event-driven paths.
2. Implement `ALARM-FU-003` and `ALARM-FU-009` to satisfy the missing operational integrations.
3. Resolve `ALARM-FU-004`, `ALARM-FU-005`, and `ALARM-FU-010` for runtime robustness.
4. Normalize duration semantics and identifier definitions.
5. Remove the mutable global reference and add the capacity guard.

