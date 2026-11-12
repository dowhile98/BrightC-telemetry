# Embedded Web Server & REST API Subsystem Progress

- **Status**: **Phase 1 Complete (Architecture & Port Contracts 100%, Implementation 0%)**
- **Architecture Reference**: [`doc/architecture/web_server_rest_api_subsystem.md`](../architecture/web_server_rest_api_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Architecture Specification** | `doc/architecture/web_server_rest_api_subsystem.md` | 100% | Dual responsibility (static SPA `/SD:/www` + REST API), Zephyr native HTTP server (`CONFIG_HTTP_SERVER=y`), zero-malloc JSON descriptors, 2 KB chunked streaming, compression, endpoints catalog |
| **Common Domain Types & DTOs** | `app/include/common/web_server_types.h` | 100% | Pure C99 enums, config, metrics, and DTOs (`rest_hvac_status_dto_t`, `rest_hvac_setpoint_dto_t`, `rest_energy_status_dto_t`, `rest_thermal_status_dto_t`, `rest_alarm_ack_dto_t`, `rest_history_query_dto_t`) |
| **JSON Serializer Interface** | `app/include/interfaces/i_json_serializer.h` | 100% | Zero-malloc abstract serializer port contract (`i_json_serializer_api_t`, `i_json_serializer_t`, validated wrappers) |
| **Web Server Interface** | `app/include/interfaces/i_web_server.h` | 100% | Abstract web server port contract (`i_web_server_api_t`, `i_web_server_t`, validated wrappers for lifecycle, metrics, and SD hot-plug notification) |

---

## 2. Pending Implementation Checklist (TDD Phases)

- [ ] **Phase 2: Zero-Malloc JSON Codec Adapter**
  - Unit tests `tests/unit/web_server/test_json_serializer.c` on `native_sim`.
  - Concrete adapter `app/src/infrastructure/adapters/adapter_json_serializer.c` implementing `i_json_serializer_t` via `<zephyr/data/json.h>`.
- [ ] **Phase 3: Dynamic REST API Callbacks & URL Routing**
  - Unit tests `tests/unit/web_server/test_rest_handlers.c` with in-memory domain fakes.
  - Concrete handlers for `/api/hvac/status`, `/api/hvac/setpoint`, `/api/energy/status`, `/api/thermal/status`, `/api/alarm/ack`, `/api/history/download`.
- [ ] **Phase 4: Static FS Streaming & Degraded Mode Simulation**
  - Unit tests `tests/unit/web_server/test_web_server_fs.c`.
  - Concrete adapter `app/src/infrastructure/adapters/adapter_web_server.c` with link-time declarations (`HTTP_SERVICE_DEFINE`, `HTTP_RESOURCE_DEFINE`).
  - Flash-resident maintenance fallback HTML page and degraded mode logic on SD ejection.
- [ ] **Phase 5: Composition Root & Target Firmware Integration**
  - Wire web server subsystem in `app/src/infrastructure/di/composition_root.c`.
  - Verify MPU stack protection and memory footprint (<50% Flash, <50% RAM) on `brightc-controllerv3`.

---

## 3. Changelog & Refactoring History

- **2026-11-12**: Initial architectural design, technical specification, and interface contracts authored:
  - Authored authoritative specification `doc/architecture/web_server_rest_api_subsystem.md`.
  - Defined pure C99 types, configuration, metrics, and REST DTOs in `app/include/common/web_server_types.h`.
  - Defined abstract port contracts `app/include/interfaces/i_json_serializer.h` and `app/include/interfaces/i_web_server.h` adhering to Section 4.1.D fat-pointer encapsulation.
  - Defined declarative link-time routing, fixed 2 KB chunked streaming, transparent `.gz` compression negotiation, and zero-malloc Zephyr JSON descriptors.
