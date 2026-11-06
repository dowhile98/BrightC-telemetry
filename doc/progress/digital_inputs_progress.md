# Digital Inputs & Contact Alarms Subsystem Progress

- **Status**: **IN_PROGRESS (85%)**
- **Architecture Reference**: [`doc/architecture/digital_input_subsystem.md`](../architecture/digital_input_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Aggregator** | `app/src/domain/services/digital_input_aggregator.c` | 100% | Temporal debounce filter (50ms default), raw level inversion, alarm state mapping |
| **Hardware Adapter** | `app/src/infrastructure/adapters/adapter_di_gpio.c` | 100% | 2x MCP23S17 SPI expanders + on-chip GPIOs (User button, switch bypass) |
| **Active Object** | `app/src/application/active_objects/ao_digital_input.c` | 100% | Periodic hardware scan (20ms), debouncing, Zbus publishing |
| **Zbus Channels** | `app/src/application/zbus/digital_input_channels.c` | 100% | `chan_digital_inputs` publishing aggregated channel bitmask |
| **Unit Tests** | `tests/unit/digital_input/` | 100% | 14 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] High-speed pulse counter mode for dry-contact flow/energy meter pulses (frequency / accumulator).

---

## 3. Changelog & Refactoring History
- **2026-11-06**: Resolved audit finding P2-11 (Bus Load & SPI Batching for MCP23S17):
  - Added 16-bit port value caching (`cached_val`, `cache_valid`, `cache_time_ms`) to `adapter_di_gpio.c/.h` using `gpio_port_get_raw()`.
  - Batching reduced SPI transactions from 24 individual reads down to 2 transactions per 20ms scan interval (1 per chip) with timestamp-invalidated cache coherency.
- **2026-11-05**: Refactored `ao_digital_input` to 5-phase canonical lifecycle.
- **2026-11-05**: Removed `DEVICE_DT_GET()` from `adapter_di_gpio.c` and injected `mcp23s17_0`, `mcp23s17_1` from `composition_root.c`.
