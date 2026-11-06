# Telemetry Store & Forward Subsystem Progress

- **Status**: **PLANNED (0%)**
- **Architecture Reference**: [`doc/architecture/telemetry_store_forward_subsystem.md`](../architecture/telemetry_store_forward_subsystem.md)

---

## 1. Implemented Components
- None. (Architecture specification and memory sizing completed).

---

## 2. Pending Work
- [ ] Implement Tier 1 RAM Ring Buffer in AXI SRAM (500 records).
- [ ] Implement Tier 2 MicroSD Spooler (`/sd/spool/` persistent files).
- [ ] Implement Active Object `ao_telemetry_spool`.
- [ ] Implement compact serialization (CBOR or compact JSON).
- [ ] Implement uplink drain controller (dispatches spooled batches when MQTT is connected).

---

## 3. Changelog & Refactoring History
- **2026-11-05**: Architectural specification created and approved.
