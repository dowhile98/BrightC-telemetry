# Firmware OTA & MCUboot Subsystem Progress

- **Status**: **PLANNED (0%)**
- **Architecture Reference**: [`doc/architecture/mcuboot_external_flash_ota_architecture.md`](../architecture/mcuboot_external_flash_ota_architecture.md)

---

## 1. Implemented Components
- None. (Proposal and partition layout approved).

---

## 2. Pending Work
- [ ] Enable Sysbuild and configure MCUboot v2.x as first stage bootloader.
- [ ] Configure devicetree and SPI driver for Winbond W25Q16 (16 Mbit NOR Flash, QSPI).
- [ ] Define flash partition layout: primary slot (internal flash), secondary slot (external flash), scratch.
- [ ] Integrate MCUmgr / SMP management agent over Wi-Fi / Ethernet for image upload and validation.

---

## 3. Changelog & Refactoring History
- **2026-11-05**: Architectural proposal approved.
