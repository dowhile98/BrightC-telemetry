# Network Subsystem Progress

- **Status**: **IN_PROGRESS (75%)**
- **Architecture Reference**: [`doc/architecture/network_subsystem.md`](../architecture/network_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Link Interface** | `app/include/interfaces/i_network.h` | 100% | Pure link lifecycle port (`init`, `start`, `stop`, `get_config`, `set_config`, `get_status`) |
| **Wi-Fi Control Interface** | `app/include/interfaces/i_wifi_control.h` | 100% | Dedicated wireless control port (`scan`, `connect_ap`, `disconnect`, `set_mode`) |
| **Ethernet Adapter** | `app/src/infrastructure/adapters/adapter_eth_stm32.c` | 100% | STM32 RMII LAN8742 driver, DHCP/Static IPv4, FSM state tracking |
| **Wi-Fi Adapter** | `app/src/infrastructure/adapters/adapter_wifi_esp_hosted.c` | 100% | ESP-Hosted-MCU SPI4, STA, Soft-AP, DHCP server, FSM state tracking |
| **Active Object** | `app/src/application/active_objects/ao_network.c` | 100% | Orchestrates link states, configuration updates, publishes to Zbus |
| **Zbus Channels** | `app/src/application/zbus/network_channels.c` | 100% | `chan_network_eth_state`, `chan_network_wifi_state` |
| **Unit Tests** | `tests/unit/network/` | 100% | 17 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] MQTT / MQTTS Telemetry Client (QoS 1, TLS 1.3).
- [ ] Local Embedded HTTP Server / REST API for web portal.
- [ ] SNTP / NTP time client for real-time clock synchronization.

---

## 3. Changelog & Refactoring History
- **2026-11-06**: Resolved audit findings P2-2, P2-8:
  - P2-2 (Wi-Fi Credentials & Default Configuration Security): Defined Kconfig symbols `CONFIG_BRIGHTC_WIFI_DEFAULT_SSID`, `CONFIG_BRIGHTC_WIFI_DEFAULT_PSK`, `CONFIG_BRIGHTC_WIFI_DEFAULT_IP` in `app/Kconfig`. Bound defaults into `default_config_profiles.c` and masked raw credentials in logs.
  - P2-8 (Eliminate Ignored Returns on Critical Operations): Checked return values of `i_network_stop`, `i_network_start`, `i_network_init`, and `i_network_set_config` in `ao_network.c`, logging errors on failure.
- **2026-11-05**: Refactored `ao_network` to 5-phase canonical lifecycle.
- **2026-11-05**: Segregated `i_network` and `i_wifi_control` adhering to ISP.
- **2026-11-05**: Implemented explicit FSM `net_adapter_fsm_state_t` in `adapter_eth_stm32` and `adapter_wifi_esp_hosted`.
