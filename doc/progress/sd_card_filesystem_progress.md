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
| **Application Facade** | `app/src/application/services/app_facade.h`, `app_facade.c` | 100% | Facade APIs (`app_facade_sd_get_status`, `app_facade_sd_mount`, `app_facade_sd_eject`) |
| **Composition Root** | `app/src/infrastructure/di/composition_root.h`, `composition_root.c` | 100% | Subsystem 12 wiring, container getters, graceful start/stop lifecycle |
| **Shell Commands** | `app/src/presentation/shell/shell_brightc.c` | 100% | `sd status`, `sd mount`, `sd eject`, `sd ls [path]` |
| **Unit Test Suite** | `tests/unit/storage_sd/` | 100% | 12 tests across `fake_storage_fs_suite` and `sd_lifecycle_sm_suite` running on `native_sim` |
| **Datalogger Architecture** | `doc/architecture/historical_datalogger_storage_subsystem.md` | 100% | 3 daily CSV streams (`control`, `energy`, `thermal`), 2 daily logs (`system`, `alarms`), zero-copy HTTP streaming, 5s anti-chatter debounce |

---

## 2. Pending Work
- Telemetry & Event Datalogger implementation (`ao_datalogger`, domain formatters, zero-copy HTTP streamer) per [`historical_datalogger_storage_subsystem.md`](../architecture/historical_datalogger_storage_subsystem.md).

---

## 3. Changelog & Refactoring History
- **2026-11-05**: Initial port contract `i_storage_fs.h` defined.
- **2026-11-12**: Implemented full MicroSD Storage & FAT Filesystem Subsystem:
  - Configured `CONFIG_DISK_ACCESS`, `CONFIG_DISK_DRIVER_SDMMC`, `CONFIG_FILE_SYSTEM`, `CONFIG_FAT_FILESYSTEM_ELM`, `CONFIG_FS_FATFS_LFN`.
  - Created domain types and interface (`storage_fs_types.h`, `i_storage_fs.h`).
  - Implemented in-memory fake (`tests/mocks/fake_storage_fs.h`, `tests/mocks/fake_storage_fs.c`).
  - Implemented SD lifecycle state machine (`sd_lifecycle_sm.h`, `sd_lifecycle_sm.c`).
  - Added unit test suite `tests/unit/storage_sd/` (100% pass on `native_sim`).
  - Implemented Zephyr VFS / FatFS adapter (`adapter_storage_sd_zephyr.h`, `adapter_storage_sd_zephyr.c`) with atomic in-flight operation counter.
  - Implemented Active Object coordinator `ao_storage_sd.h/.c` and Zbus channel `chan_storage_sd_status`.
  - Integrated with `app_facade`, `composition_root`, and registered shell commands `sd status`, `sd mount`, `sd eject`, `sd ls`.
  - Target firmware build verified for `brightc-controllerv3/stm32h743xx` (0 warnings, 0 errors).
- **2026-11-12**: Authored authoritative specification for Historical Telemetry & Event-Driven Datalogger (`doc/architecture/historical_datalogger_storage_subsystem.md`):
  - Defined 3 segregated daily CSV streams (`_control.csv`, `_energy.csv`, `_thermal.csv`) and 2 daily log streams (`_system.log`, `_alarms.log`).
  - Aligned MicroSD standard directory layout under `/SD:/history/YYYY/MM/` and `/SD:/logs/YYYY/MM/`.
  - Defined 5s anti-chatter debounce filter, 512-byte sector-aligned RAM staging buffers, and midnight UTC rollover protocol.
  - Specified STM32H743 Zero-Copy HTTP Streaming engine for embedded web server (2 KB chunk DMA direct to socket).
