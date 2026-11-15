# MicroSD Storage & Filesystem Subsystem Progress

- **Status**: **COMPLETE (100%)**
- **Architecture Reference**: [`doc/architecture/sd_card_filesystem_subsystem.md`](../architecture/sd_card_filesystem_subsystem.md), [`doc/architecture/historical_datalogger_storage_subsystem.md`](../architecture/historical_datalogger_storage_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Common Types** | `app/include/common/storage_fs_types.h` | 100% | File flags, seek origins, stat, dirent, volume info, card status enum |
| **Storage FS Interface** | `app/include/interfaces/i_storage_fs.h` | 100% | Pure VFS abstract port fat pointer (`mount`, `unmount`, `open`, `read`, `write`, `sync`, `stat`, `statvfs`, etc.) |
| **In-Memory Fake FS** | `tests/mocks/fake_storage_fs.h`, `tests/mocks/fake_storage_fs.c` | 100% | RAM virtual filesystem with presence toggling and fault injection |
| **Lifecycle State Machine** | `app/include/domain/storage/sd_lifecycle_sm.h`, `app/src/domain/storage/sd_lifecycle_sm.c` | 100% | Pure domain FSM for card detection, 150 ms debounce, mount/unmount, surprise removal |
| **Infrastructure Adapter** | `app/src/infrastructure/adapters/adapter_storage_sd_zephyr.h`, `adapter_storage_sd_zephyr.c` | 100% | Zephyr VFS / FatFS adapter, atomic in-flight operation tracking |
| **Zbus Notification** | `app/src/application/zbus/storage_channels.h`, `storage_channels.c` | 100% | `chan_storage_sd_status` channel broadcasting `sd_card_status_msg_t` |
| **Active Object Coordinator** | `app/src/application/active_objects/ao_storage_sd.h`, `ao_storage_sd.c` | 100% | Dedicated coordinator thread, 200 ms poll/tick, automatic folder provisioning (`/SD:/history`, `/logs`, `/www`) |
| **Application Facade** | `app/include/application/services/app_facade.h`, `app/src/application/services/app_facade.c` | 100% | Facade APIs (`app_facade_sd_get_status`, `app_facade_sd_mount`, `app_facade_sd_eject`) |
| **Composition Root** | `app/src/infrastructure/di/composition_root.h`, `composition_root.c` | 100% | Subsystems 12, 13, and 14 wiring, container getters, graceful start/stop lifecycle |
| **Shell Commands** | `app/src/presentation/shell/shell_brightc.c` | 100% | `sd status`, `sd mount`, `sd eject`, `sd ls [path]` |
| **HVAC Status Port Contract** | `app/include/interfaces/i_hvac_status.h` | 100% | Abstract port fat pointer (`i_hvac_status_t`) decoupling telemetry sampler from `ao_hvac` internals |
| **Datalogger Types** | `app/include/common/datalogger_types.h` | 100% | Packed `datalogger_telemetry_record_t` with HVAC governing temperatures (`t_ctrl_g0`, `t_ctrl_g1`, `t_inj_mean`, `t_ret_mean`, `delta_t_clima`), 64-byte `alarm_nv_record_t`, envelopes |
| **BKPSRAM Ring Buffer** | `app/src/domain/storage/bkpsram_ring.h/.c` | 100% | Pure C circular buffer in STM32 Backup SRAM with CRC-16-CCITT and corruption detection |
| **BKPSRAM Hardware Adapter** | `app/include/interfaces/i_bkpsram.h`, `app/src/infrastructure/adapters/adapter_bkpsram_stm32.h/.c` | 100% | STM32H7 Backup SRAM peripheral driver (`0x38800000`, `RCC_AHB4ENR_BKPRAMEN`, `PWR_CR1_DBP`, `PWR_CR2_BREN`) |
| **BKPSRAM Mock Fake** | `tests/mocks/fake_bkpsram.h/.c`, `tests/fakes/include/fake_bkpsram.h` | 100% | In-memory RAM adapter for host unit tests |
| **Active Object Datalogger** | `app/src/application/active_objects/ao_datalogger.h/.c` | 100% | Canonical 5-phase lifecycle, 1024-byte staging buffers in `.nocache` RAM, 30s flush timer, FIFO queue serialization, BKPSRAM recovery on mount, 100 MB low-watermark auto-pruning |
| **Operational System Channel** | `app/src/application/zbus/system_channels.h/.c` | 100% | `chan_system_event` broadcasting system operational and storage events |
| **Datalogger Formatters** | `app/include/domain/datalogger/datalogger_formatters.h`, `app/src/domain/datalogger/datalogger_formatters.c` | 100% | Pure C99 ASCII CSV row formatters for unified 27-column telemetry, forensic alarm SOE, and system events |
| **Telemetry Sampler Service** | `app/include/application/services/telemetry_sampler.h`, `app/src/application/services/telemetry_sampler.c` | 100% | Aggregates periodic multi-source telemetry via `i_hvac_status_t` (highest temps, mean coil temps, delta-T, unit states), thermal metrics ($Q_{sens}, Q_{lat}, COP, \dot{m}_{da}, T_{dew}$), 3-phase electrical energy ($V, I, P_{tot}, PF, kWh$), and active alarms |
| **Datalogger Streamer Service** | `app/include/application/services/datalogger_streamer.h`, `app/src/application/services/datalogger_streamer.c` | 100% | 1024-byte chunk streamer with strict YYYY-MM-DD traversal sanitization and active-day AO flush synchronization |
| **Unit Test Suite** | `tests/unit/datalogger/`, `tests/unit/storage_sd/` | 100% | 35 total unit tests (23 datalogger tests across 5 suites + 12 storage_sd tests across 2 suites) running on `native_sim` |

---

## 2. Pending Work
- None. Historical telemetry logging with HVAC governing temperature capture, Backup SRAM forensic recovery, and chunked HTTP streamer services are fully implemented and verified.

---

## 3. Changelog & Refactoring History
- **2026-11-14 (Stage 7 Complete)**: Resolved all Stage 6 Critic Findings & Integrated HVAC Governing Temperatures:
  - Created Clean Architecture port contract `i_hvac_status.h` (`i_hvac_status_t`, `i_hvac_status_api_t`, `i_hvac_status_get`) decoupling telemetry sampler from `ao_hvac` internals.
  - Expanded `datalogger_telemetry_record_t` and `DATALOGGER_CSV_HEADER_TELEMETRY` with governing temperatures: `t_ctrl_g0`, `t_ctrl_g1` (highest control temp per HVAC group), `t_inj_mean`, `t_ret_mean`, and `delta_t_clima`.
  - Updated `telemetry_sampler.c` to ingest HVAC snapshot via `i_hvac_status_t` and compute mean coil supply/return and delta-T metrics.
  - Sized staging buffers to 1024 bytes (`AO_DATALOGGER_BUFFER_SIZE`) in non-cacheable RAM (`.nocache`).
  - Added bounds checking in `ao_datalogger.c` `append_to_stream()` to prevent buffer overflow.
  - Serialized `ao_datalogger_flush()` via `DATALOGGER_EVT_FLUSH_TRIGGER` in message queue for strict FIFO execution.
  - Harmonized file path generation (`%s/history/%04d/%02d/%s_telemetry.csv`) across both `ao_datalogger.c` and `datalogger_streamer.c`.
  - Implemented reader/writer concurrency synchronization in `datalogger_streamer.c` (flushing and closing active file before streaming).
  - Wired `chan_hvac_event` in `app_event_mediator.c` to forward HVAC state transitions to `ao_datalogger` as `DATALOGGER_EVT_HVAC_EVENT`.
  - Updated unit test suites in `tests/unit/datalogger/` to verify all new fields and edge cases (23/23 passed, 100%).
  - Verified clean target cross-compilation for `brightc-controllerv3` (STM32H743) (Flash: 27.33%, RAM: 58.26%, SRAM3: 50.00%).
  - Synchronized `doc/architecture/historical_datalogger_storage_subsystem.md` with final code as the single source of truth.
- **2026-11-14**: Implemented Historical Telemetry Datalogger, Sampler Aggregator, and Chunked Streamer:
  - Added `LOG_STREAM_TELEMETRY`, `datalogger_telemetry_record_t`, and `DATALOGGER_EVT_TELEMETRY_SAMPLE` to `datalogger_types.h`.
  - Implemented pure C99 CSV formatters `datalogger_formatters.h/.c` for telemetry, forensic alarms, and system events.
  - Implemented `telemetry_sampler.h/.c` aggregating HVAC group/units, thermal metrics ($Q_{sens}, Q_{lat}, COP, \dot{m}_{da}, T_{dew}$), 3-phase electrical energy ($V, I, P_{tot}, PF, kWh$), and active alarm bitmask.
  - Implemented zero-heap chunked streamer `datalogger_streamer.h/.c` with path traversal sanitization and active-day buffer flush synchronization.
- **2026-11-14 (Initial)**: Implemented Unified Device Event & Forensic Logging Subsystem on MicroSD and Backup SRAM:
  - Created packed 64-byte `alarm_nv_record_t` and `datalogger_event_envelope_t` in `datalogger_types.h`.
  - Implemented pure C Backup SRAM circular buffer `bkpsram_ring.h/.c` with CRC-16-CCITT integrity checks.
  - Implemented abstract port contract `i_bkpsram.h` and STM32H7 hardware driver `adapter_bkpsram_stm32.h/.c`.
  - Developed Active Object coordinator `ao_datalogger.h/.c` with 512-byte stream buffers (`_alarms.log`, `_system.log`, `boot_staging.csv`), BKPSRAM crash forensic recovery, and 100 MB low-watermark prune.
- **2026-11-12**: Implemented full MicroSD Storage & FAT Filesystem Subsystem.
