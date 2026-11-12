# Network Subsystem Progress

- **Status**: **IN_PROGRESS (85%)**
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
| **Network Facade** | `app/include/application/services/app_facade.h`, `app/src/application/services/app_facade.c` | 100% | `app_facade_network_*` APIs: status, scan, connect, disconnect, save |
| **Network Shell Commands** | `app/src/presentation/shell/shell_brightc.c` | 100% | Productive `network status/wifi connect/wifi scan/wifi disconnect/wifi save` CLI with SSID/PSK validation and EEPROM persistence |
| **Native Zephyr Shell** | `app/prj.conf` | 100% | `CONFIG_NET_L2_WIFI_SHELL=y`, `CONFIG_NET_SHELL=y` enabled for diagnostics |
| **DI Container Getters** | `app/src/infrastructure/di/composition_root.c/h` | 100% | `app_container_get_network_ao()`, `app_container_get_storage_ao()` |
| **Unit Tests** | `tests/unit/network/` | 100% | 17 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] MQTT / MQTTS Telemetry Client (QoS 1, TLS 1.3).
- [ ] Local Embedded HTTP Server / REST API for web portal.
- [ ] SNTP / NTP time client for real-time clock synchronization.

---

## 3. Changelog & Refactoring History
- **2026-11-12**: Implemented productive WiFi/Network shell commands:
  - Added `app_container_get_network_ao()` and `app_container_get_storage_ao()` getters in `composition_root.c/h`.
  - Added `app_facade_network_get_status()`, `app_facade_network_wifi_scan()`, `app_facade_network_wifi_connect()`, `app_facade_network_wifi_disconnect()`, `app_facade_network_wifi_config_save()` in `app_facade.c/h`.
  - Added `network` root shell command tree in `shell_brightc.c` with `status`, `wifi scan`, `wifi connect <ssid> [psk]`, `wifi disconnect`, `wifi save`.
  - Enabled native Zephyr diagnostic shells via `CONFIG_NET_L2_WIFI_SHELL=y` and `CONFIG_NET_SHELL=y` in `prj.conf`.
  - WiFi connect applies configuration through `ao_network`, starts connection via `i_wifi_control_connect_ap`, marks `CONFIG_DOMAIN_NET_WIFI` dirty in `ao_storage`, and forces immediate flush on `network wifi save`.
  - Target build verified: `brightc-controllerv3` FLASH 549324 B (26.19%), RAM 286524 B (54.65%); `unit.network.all` passes 17/17 on `native_sim`.
- **2026-11-06**: Resolved audit findings P2-2, P2-8:
  - P2-2 (Wi-Fi Credentials & Default Configuration Security): Defined Kconfig symbols `CONFIG_BRIGHTC_WIFI_DEFAULT_SSID`, `CONFIG_BRIGHTC_WIFI_DEFAULT_PSK`, `CONFIG_BRIGHTC_WIFI_DEFAULT_IP` in `app/Kconfig`. Bound defaults into `default_config_profiles.c` and masked raw credentials in logs.
  - P2-8 (Eliminate Ignored Returns on Critical Operations): Checked return values of `i_network_stop`, `i_network_start`, `i_network_init`, and `i_network_set_config` in `ao_network.c`, logging errors on failure.
- **2026-11-05**: Refactored `ao_network` to 5-phase canonical lifecycle.
- **2026-11-05**: Segregated `i_network` and `i_wifi_control` adhering to ISP.
- **2026-11-05**: Implemented explicit FSM `net_adapter_fsm_state_t` in `adapter_eth_stm32` and `adapter_wifi_esp_hosted`.
