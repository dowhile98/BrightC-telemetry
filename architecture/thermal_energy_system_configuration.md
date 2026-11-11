# Thermal and Energy System: Configuration and Persistence

## Configuration ownership

| Parameter class | Source | Examples | Persistence / validation |
|---|---|---|---|
| Build and hardware capability | Kconfig / DTS | enable feature, maximum pairs/history, selected J9 humidity slot | Build-time; selected logical J9 index must be present in DTS ADC DMA map. |
| Site topology | Persistent configuration | sensor IDs for pairs/room/outdoor, pair-to-unit mapping, fan/compressor role binding, nominal airflows and capacities, redundancy count | Validate catalog IDs, uniqueness, complete pairs, limits and enabled profiles. |
| Calibration | Existing calibration domain | PT100 offset/gain, humidity offset/gain/shunt conversion parameters | Valid ranges; write only on commissioning/calibration change. |
| Learned baseline | Separate persistent payload or versioned HVAC extension | OLS beta, baseline eta, pair baselines, validated training metadata | Persist after accepted hourly training checkpoints, not each sample. |
| Runtime state | RAM | latest capture/results, stabilization timers, 720 hourly samples | Cleared/rebuilt or restored from durable history; never EEPROM at 1 Hz. |

## Kconfig proposal

`CONFIG_THERMAL_ENERGY` enables the subsystem. `CONFIG_THERMAL_ENERGY_HUMIDITY_J9_INDEX` selects 1..10, defaults to 1, and depends on the chosen board’s ADC DMA engine. Kconfig selects a hardware capability only; it does not hold field-calibration or site-topology data. During implementation, use Kconfig validation plus a compile-time DTS map assertion so a channel cannot be selected if absent.

## Logical sensor topology

Use existing flat IDs from `sensor_hardware_map.h`, not physical ADC/Modbus positions inside formulas. Persist arrays of `{supply_id, return_id, hvac_unit_id}`, `room_id[]`, and `outdoor_id[]`; use `0` as unassigned. Reject duplicate assignment across incompatible roles, absent catalog IDs, duplicate source within a pair, or non-contiguous configuration that exceeds maxima. A measurement can remain configured while temporarily invalid; that changes result quality rather than topology.

## Storage strategy

Use the existing Microconf EEPROM adapter and envelope semantics (magic/version/domain/CRC). `CONFIG_DOMAIN_CALIBRATION` can retain physical calibration; topology and fixed site constants belong in a deliberately versioned thermal payload associated with HVAC configuration only after confirming its available 576-byte slot. If that capacity is insufficient, allocate a new domain through the existing layout mechanism, with migration and tests; do not place new data at an unreviewed EEPROM offset.

The learned baseline needs its own validity metadata: topology/config revision, trained hours, last update, algorithm/schema version, regularization value and CRC envelope. Invalidate it when topology, calibration, nominal airflow/capacity, or algorithm schema changes.

## Current-profile caveats

The sensor mapper’s default pilot topology is close to REVF 6+6+2+2, while the default hybrid HVAC profile selects five internal RTDs. Deployment must select and persist one coherent thermal topology; mixing these defaults would produce unrelated HVAC-control and analytical inputs.

