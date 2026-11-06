# Self-Test & Diagnostic Subsystem Progress

- **Status**: **PLANNED (0%)**
- **Architecture Reference**: [`doc/architecture/selftest_subsystem.md`](../architecture/selftest_subsystem.md)

---

## 1. Implemented Components
- None. (Architecture specification and test checklist completed).

---

## 2. Pending Work
- [ ] Implement declarative test registry `selftest_registry`.
- [ ] Implement POST boot gate checks: $V_{REFINT}$, I2C2 EEPROM probe, SPI MCP23S17 ping, USART loopback.
- [ ] Implement fault isolation and classification (`CRITICAL_HALT` vs `DEGRADED`).
- [ ] Publish results to Zbus and map to indicator LED/Buzzer patterns.

---

## 3. Changelog & Refactoring History
- **2026-11-05**: Architectural specification created and approved.
