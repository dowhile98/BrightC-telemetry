# Firmware Implementation Progress & Traceability Matrix

- **Project**: DAECGE BrightC Controller V3 Firmware (STM32H743ZIT6)
- **Environment**: Zephyr RTOS v4.x (West T2 Workspace)
- **Standard**: Clean Architecture + Ports & Adapters + Active Object Pattern + TDD
- **Rule**: Every new implementation, extension, or refactoring MUST update the corresponding progress file in this directory and sync this table.

---

## 1. Master Progress Dashboard

| Subsystem / Layer | Progress File | Status | Codebase Coverage | Last Updated |
| :--- | :--- | :---: | :--- | :---: |
| **Core Architecture & Framework** | [`core_architecture_progress.md`](core_architecture_progress.md) | **100%** | Active Objects (5/5), DI Container, Hexagonal Ports & Adapters | 2026-11-07 |
| **System Indicators** | [`system_indicators_progress.md`](system_indicators_progress.md) | **100%** | `ao_system_indicator`, LED GPIO & Buzzer PWM adapters, Zbus | 2026-11-05 |
| **Industrial Modbus** | [`modbus_progress.md`](modbus_progress.md) | **90%** | Dual RS-485, 4 codecs, scheduler, registry, relay adapter, `ao_modbus`, mode-aware slave topology | 2026-11-11 |
| **Digital Inputs** | [`digital_inputs_progress.md`](digital_inputs_progress.md) | **95%** | `ao_digital_input`, Zbus snapshot & transitions, HVAC safety interlock listener, 2x MCP23S17 SPI + Modbus MBSL32DI, profile-specific pin tables (control/telemetry) | 2026-11-11 |
| **Network & Wi-Fi** | [`network_progress.md`](network_progress.md) | **75%** | `ao_network`, STM32 RMII Ethernet, ESP-Hosted SPI Wi-Fi, ISP ports | 2026-11-06 |
| **Storage & Persistence** | [`storage_progress.md`](storage_progress.md) | **100%** | `ao_storage`, Microconf I2C2 EEPROM adapter, 7 domains (incl. thermal site & baseline), degraded mode | 2026-11-11 |
| **Sensors & Mathematical Model** | [`sensors_progress.md`](sensors_progress.md) | **100%** | Zbus channels (`chan_thermal_energy_update`), sensor matrix mapper, flat 64-ch snapshot, Rev F Thermodynamic Engine, Phase 1 Types/EEPROM, Phase 2 Math Model, Phase 3 OLS Predictive Analytics, Phase 4 Port Contract & CMSIS-DSP, Phase 5 AO `ao_thermal_energy`, 720h RAM ring buffer, DI integration | 2026-11-11 |
| **MicroSD Filesystem** | [`sd_card_filesystem_progress.md`](sd_card_filesystem_progress.md) | **10%** | Port `i_storage_fs.h` & types. Pending SDMMC1 FatFS & `ao_storage_sd` | 2026-11-05 |
| **Telemetry Store & Forward** | [`telemetry_store_forward_progress.md`](telemetry_store_forward_progress.md) | **0%** | Spec approved. Pending dual-tier spooling & `ao_telemetry_spool` | 2026-11-05 |
| **Self-Test & Diagnostics** | [`selftest_progress.md`](selftest_progress.md) | **0%** | Spec approved. Pending POST registry, rail/bus sanity checks | 2026-11-05 |
| **OTA & MCUboot** | [`ota_mcuboot_progress.md`](ota_mcuboot_progress.md) | **0%** | Spec approved. Pending Sysbuild MCUboot, QSPI W25Q16, MCUmgr | 2026-11-05 |
| **Industrial HVAC Control** | [`hvac_control_progress.md`](hvac_control_progress.md) | **100%** | Full domain FSMs, Multi-Room Range Engine, Facade & Shell, ao_hvac, 63 unit tests, profile-driven mode sync & dynamic actuator init | 2026-11-11 |
| **Alarm Management** | [`alarm_management_progress.md`](alarm_management_progress.md) | **100%** | Centralized AO, discrete & multi-source aggregate FSMs, 4 thermal groups / salas, 8 HVAC units, hysteresis evaluators, 4 detector adapters, Zbus channels, DIP query port, Shell CLI presentation, persistent buzzer mute, 17 unit tests | 2026-11-10 |

---

## 2. Maintenance Rules for Developers & Agents
1. **Always Update on Change**: When adding a feature, fixing a bug, or refactoring code (even if the subsystem is at 100%), add a concise entry to the subsystem's *Changelog* section.
2. **Keep it Concise**: Focus on deliverables, file paths, test status, and remaining work. Do not write verbose narratives.
3. **Traceability**: Link every progress document to its formal architecture specification in `doc/architecture/`.
