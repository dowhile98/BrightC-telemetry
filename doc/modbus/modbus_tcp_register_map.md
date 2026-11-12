# BrightC Modbus TCP Server Register Map

This document describes the Modbus TCP behavior implemented by the firmware callbacks in `app/src/infrastructure/adapters/adapter_modbus_tcp_server.c`. The register numbers below use the traditional reference notation (0xxxx coils, 1xxxx discrete inputs, 3xxxx input registers, 4xxxx holding registers). The PDU address is zero-based: for example, reference 40001 is PDU address 0. Clients that use zero-based addresses should send the PDU address directly.

## Connection and protocol

- TCP port: **502** (`MODBUS_TCP_DEFAULT_PORT`). The composition root configures this port.
- The server uses Zephyr's nanoMODBUS TCP server. The Modbus unit identifier is not used by the register callbacks.
- Implemented function codes: **0x01** Read Coils, **0x02** Read Discrete Inputs, **0x03** Read Holding Registers, **0x04** Read Input Registers, **0x05** Write Single Coil, **0x06** Write Single Register, **0x0F** Write Multiple Coils, **0x10** Write Multiple Registers.
- Invalid data addresses return Modbus exception 0x02; zero-length reads and invalid values return 0x03; failed facade operations generally return 0x04.
- Registers are 16-bit. Multi-register 32-bit quantities use the configured `word_order`; the default is big-endian word order unless the server configuration overrides it. Modbus bytes within each register remain in protocol order.

## Coils (FC 0x01, 0x05, 0x0F)

| Reference | PDU address | Access | Meaning / behavior |
|---|---:|---|---|
| 00001–00024 | 0–23 | Read/write | HVAC unit relay values in groups of three per unit: `AA`, `SM`, `G`; unit index is `address / 3`, relay is `address % 3`. Reads report current tri-state relay outputs. Writes to `AA` or `G` update manual override; `SM` is readable but a write does not change the selected relay values. Setting AA and/or G enables manual mode; setting both false disables it. |
| 00050 | 49 | Read/write | Alarm buzzer mute. Read is 1 when muted. Write 1 mutes; write 0 unmutes. |
| 00060–00063 | 59–62 | Read/write | HVAC group 0–3 forced rotation pulse. Read always returns 0. Writing 1 invokes one rotation for that group; writing 0 is a no-op. |
| 00070 | 69 | Read/write | Storage save-all trigger. Read always returns 0. Writing 1 invokes storage save; writing 0 is a no-op. |

FC 0x0F is implemented by applying each coil in the requested range sequentially. A request that spans an unmapped gap or a range boundary can therefore apply earlier writes before returning an illegal-address exception.

## Discrete inputs (FC 0x02)

| Reference | PDU address | Access | Meaning / behavior |
|---|---:|---|---|
| 10001–10056 | 0–55 | Read-only | Physical/logical digital input snapshot, one bit per channel. Channels beyond the snapshot's reported `channel_count` read as 0. |
| 10101–10180 | 100–179 | Read-only | Active alarm flags indexed by the alarm catalog order, not by alarm ID. A bit is 1 when its catalog alarm has a non-inactive active-list entry. Positions beyond the current catalog size or with a null catalog entry read 0. |
| 10201–10224 | 200–223 | Read-only | Current HVAC tri-state relay outputs, three per unit: `AA`, `SM`, `G`, in unit order. |

## Input registers (FC 0x04)

Values are unsigned 16-bit words unless noted. Signed temperatures and other signed values are returned as two's-complement `int16`. Unimplemented positions within an accepted block read 0.

### Sensor temperatures — 30001–30064 (PDU 0–63)

One register per sensor-matrix channel, signed temperature in **0.1 °C**. A valid channel is rounded to the nearest tenth. Invalid channels return `0x7FFF`.

### Return humidity — 30070–30071 (PDU 69–70)

| Reference | PDU | Value |
|---|---:|---|
| 30070 | 69 | Return-air relative humidity in 0.1% RH units. |
| 30071 | 70 | Humidity quality flag: 1 when quality is `QUALITY_VALID`, otherwise 0. |

### Electrical measurements — 30101–30130 (PDU 100–129)

| Reference | PDU | Value |
|---|---:|---|
| 30101–30103 | 100–102 | Phase voltages L1, L2, L3 in 0.1 V. |
| 30104–30106 | 103–105 | Phase currents L1, L2, L3 in 0.01 A. |
| 30107 | 106 | Total active power in 0.1 kW. |
| 30108 | 107 | Power factor ×1000, signed. |
| 30109 | 108 | Frequency, currently fixed at 600 (60.0 Hz). |
| 30110–30111 | 109–110 | Total active energy in 0.1 kWh, unsigned 32-bit, word order configurable. |
| 30112–30130 | 111–129 | Reserved; read as 0. |

### Thermodynamic results — 30201–30230 (PDU 200–229)

| Reference | PDU | Value |
|---|---:|---|
| 30201 | 200 | Real COP ×100. |
| 30202 | 201 | Sensible COP ×100. |
| 30203 | 202 | Carnot COP ×100. |
| 30204–30206 | 203–205 | Total sensible, latent, and total heat transfer in 0.1 units of the source result fields. |
| 30207 | 206 | Second-law efficiency in 0.1 percentage points. |
| 30208 | 207 | Total dry-air mass flow ×100. |
| 30209 | 208 | Total dry-air mass flow ×3000 (additional scaled representation). |
| 30210–30216 | 209–215 | Injection mean temperature, return mean temperature, mean climate ΔT, room mean, room max, room min, and bypass ΔT; signed, 0.1 °C. |
| 30217 | 216 | SHF ×1000. |
| 30218 | 217 | Condensate rate in 0.1 L/h. |
| 30219–30230 | 218–229 | Reserved; read as 0. |

### System diagnostics — 30301–30320 (PDU 300–319)

| Reference | PDU | Value |
|---|---:|---|
| 30301–30302 | 300–301 | Uptime in seconds, unsigned 32-bit, configurable word order. |
| 30303–30304 | 302–303 | Ethernet IPv4 address, high and low 16-bit words (`192.168.1.100` in current implementation). |
| 30305–30306 | 304–305 | Wi-Fi IPv4 address, high and low 16-bit words (`192.168.4.1` in current implementation). |
| 30307–30308 | 306–307 | SD total capacity in MiB, unsigned 32-bit, configurable word order. |
| 30309–30310 | 308–309 | SD free capacity in MiB, unsigned 32-bit, configurable word order. |
| 30311 | 310 | SD status: 3 mounted; 1 for a state other than mounted or not-inserted; 0 not inserted. |
| 30312–30320 | 311–319 | Reserved; read as 0. |

### Alarm diagnostics — 30401–30410 (PDU 400–409)

| Reference | PDU | Value |
|---|---:|---|
| 30401 | 400 | Active alarm count. |
| 30402 | 401 | Unacknowledged count, computed as `max(active_count - acknowledged_count, 0)`. |
| 30403 | 402 | Highest active alarm level. |
| 30404 | 403 | Buzzer/alarm state code: 0 muted; 1 unmuted with at least one active alarm; 2 unmuted with no active alarm. |
| 30405–30409 | 404–408 | Five 16-bit active-alarm masks in alarm catalog order, 16 catalog positions per word, least significant bit first. A bit is set for a matching non-inactive active alarm. |
| 30410 | 409 | Reserved; read as 0. |

## Holding registers (FC 0x03, 0x06, 0x10)

### HVAC group thermal bounds — 40001–40040 (PDU 0–39)

Each group occupies 10 PDU addresses. For group `g` (0–3), base address is `g × 10`; offsets 0–3 are respectively `Tmin`, `Tmax`, `Talm`, and `Tbyp`; offsets 4–9 are reserved and read as 0. Values are signed tenths of °C. Single-register writes accept −20.0 through 80.0 °C and update the selected bound through the HVAC facade. Writes to reserved offsets return illegal data address.

### HVAC group configuration — 40041–40060 (PDU 40–59)

Each group occupies five registers; group `g` base is `40 + g × 5`.

| Group offset | Meaning | Write constraints / encoding |
|---:|---|---|
| 0 | HVAC mode/state enum | Values 0–5 accepted. |
| 1 | Active turn count | Values 1–8. |
| 2 | Unit mask | Values 1–255. |
| 3 | Rotation interval in days | Values 1–365. |
| 4 | Fan delay | Bit 15 enables fan delay; bits 0–14 are delay seconds. |

Reads expose the current group state, active turn count, unit mask, rotation days, and packed fan-delay field respectively.

### RTC — 40101–40110 (PDU 100–109)

| Reference | PDU | Value |
|---|---:|---|
| 40101–40106 | 100–105 | Year, month, day, hour, minute, second. |
| 40107–40108 | 106–107 | Unix epoch seconds, low/high words when little word order is configured and high/low otherwise. |
| 40109 | 108 | Sub-millisecond value from the time service. |
| 40110 | 109 | RTC synchronization state enum. |

Single-register writes in this range are acknowledged as no-ops. FC 0x10 sets calendar date/time only when the request starts at PDU address 100 and has at least six registers; it consumes the first six words. Epoch, sub-millisecond, and synchronization fields are not set by this block.

### Supervisory command block — 40201–40205 (PDU 200–204)

| Reference | PDU | Meaning |
|---|---:|---|
| 40201 | 200 | Command opcode. A single-register write stores and immediately executes it using the currently stored argument. |
| 40202 | 201 | Command argument. A single-register write stores it for a later opcode write. |
| 40203 | 202 | Status: 0 ready/initial, 1 success, 2 error. |
| 40204 | 203 | Last executed opcode. |
| 40205 | 204 | Last error as positive errno number; 0 on success. |

Supported opcodes are `0` no operation, `1` acknowledge alarm ID (`arg` is ID), `2` acknowledge all alarms, `3` reset latched alarm (`arg` is ID, 0 means all), `4` reboot, `5` save all storage, and `6` trigger thermal baseline recalibration. FC 0x10 beginning at PDU address 200 with at least two registers writes opcode and argument together, then executes once. Other multiple-register writes are processed one register at a time.

## Implementation notes and limits

- The declared map spans are larger than the set of populated values in some blocks; the reserved locations listed above are returned as zero.
- The diagnostic IP values are constants in the current callback implementation, not queried from active network interfaces.
- Alarm discrete-input numbering and alarm-mask bit positions follow the runtime catalog order. They are not the numeric alarm IDs; use the alarm catalog definition in `app/include/common/alarm_types.h` when interpreting IDs supplied to supervisory commands.
- FC 0x10's RTC and supervisory special cases require a start address and minimum quantity as described. Other writes are dispatched register by register and are not atomic.
- The current `ao_modbus_tcp` port reports per-session inspection and client-disconnect operations as unsupported (`-ENOTSUP`).

## Source of truth

- `app/include/common/modbus_tcp_types.h` — address spans, defaults, and command opcodes.
- `app/src/infrastructure/adapters/adapter_modbus_tcp_server.c` — actual read/write mapping and validation.
- `app/src/infrastructure/di/composition_root.c` — server configuration used by the application.
- `app/src/application/active_objects/ao_modbus_tcp.c` — TCP server lifecycle and currently unsupported session operations.
