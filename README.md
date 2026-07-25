# ESPHome Pentair Pool

An ESPHome external component that controls Pentair pool equipment over RS-485
the way an IntelliCenter controller does. The ESP acts as the bus master: it
polls each device on a state-dependent cadence, tracks device state, and holds
off chlorination unless a pump reports that it is running.

The protocol and behavioral rules were derived by reverse engineering the
IntelliCenter OCP firmware.

## Firmware provenance & protocol overview

The protocol was reconstructed by statically reverse engineering the
IntelliCenter Outdoor Control Panel (OCP) firmware:

- **Binary:** `ICmain_3_008.elf`, carved from the `Update_OCP_3_008.pfw` update
  image (IntelliCenter firmware **v3.008**).
- **Format:** 32-bit ARM little-endian, EABI5, dynamically linked, **stripped**,
  Qt5 + SQLite, built with Timesys GCC 4.8.4.
- **Tooling / method:** opened in **Ghidra 12.1.2**; C++ RTTI/vtable
  recovery, targeted decompilation, and string cross-referencing. Protocol facts
  were read from decompiled code and data structures rather than adopted from
  existing implementations; where a fact was already public, the firmware
  reading was compared against it. See
  [Prior work and credits](#prior-work-and-credits).

The OCP turned out to run two independent RS-485 buses (`/dev/ttyO1`,
`/dev/ttyO2`) driven by a TX/RX packet state machine. Each device handler
registers per-command message templates (`dest`, `src`, `cmd`, `len`, payload)
that a serializer turns into wire bytes. Internally the firmware emits a
co-processor wrapper (`1E <seq16> <len16> …`); a personality board below that
applies the physical device framing this component targets (see
[Wire framing](#wire-framing)).

The packets recovered were as follows.

*IntelliFlo pumps (start byte `0xA5`, family `6`):*

| cmd | Firmware name | Payload | Meaning |
|:---:|---------------|---------|---------|
| `0x04` | HandOn / Hand Off | `0xFF` / `0x00` | remote-control enable (keepalive) |
| `0x05` | (unnamed; binder `FUN_0096baf0`) | `0x06` | VF only: selects Feature 1 |
| `0x06` | Start/Stop, Stop | 1 B | run control |
| `0x07` | Status | poll (reply carries data) | RPM / GPM / Watts, run state, drive alarms |
| `0x08` | Read Version | `02 70` | firmware version request |
| `0x09` | Flow (VSF) | `03 27 <hi> <lo>` | target GPM (BE16) |
| `0x0A` | Speed (VSF) | `03 27 <hi> <lo>` | target RPM (BE16, 450–3450) |

  The setpoint prefix is a per-generation register selector: **VS** uses cmd
  `0x01` prefix `02 C4` (RPM), **VF** uses cmd `0x01` prefix `02 E4` (GPM), **VSF**
  uses `03 27` under cmd `0x09`/`0x0A`.

*IntelliChlor (start byte `0x10`, family `2`):*

| cmd | Direction | Payload | Meaning |
|:---:|-----------|---------|---------|
| `0x11` | controller → cell | 1 B | output level **0–100 %** (100 = super-chlorinate) |
| `0x12` | cell → controller | reply | salt ppm (byte `+0x88`) + alarm mask (byte `+0x89`) |

  Decoded chlorinator alarms, using the panel's own labels: **No Flow, Low
  Salt, Very Low Salt, Inspect Cell, Cold Water Cutoff**. The firmware reads
  bits 3, 5 and 7 but never surfaces them, so neither does this component. Loss
  of communication is not part of the alarm mask; it is reported separately by
  the `comm` binary sensor.

Heaters (UltraTemp / Hybrid / MasterTemp / MaxE-Therm / ETI250) and IntelliChem
are polled on their own firmware cadences; their reply layouts are decoded as
described under [What it does](#what-it-does) and [caveats](#status--caveats).

## What it does

### Bus mastering and poll cadence

The ESP owns the RS-485 segment and continuously polls pumps and commands the
chlorinator, exactly like the OCP. Idle pumps get a status-only poll every 16 s,
the interval the OCP uses by default. When a pump is running, or a setpoint or
run-state change is pending, the hub drops to a 2 s cadence and emits the full
command batch of remote-enable, setpoint, run and status. The firmware behaves
the same way, speeding its poll timer to 2 s and sending commands event-driven
rather than blasting every packet on a fixed timer.

Every active batch opens with the HandOn command (cmd 4 = `0xFF`), again
following the OCP. An IntelliFlo reverts to its own local program if that frame
stops arriving, so re-asserting it on the fast cadence is what keeps the pump
under remote control. Once the pump goes idle the hub stops refreshing it and
returns to a status-only poll, letting the pump fall back to local control.

### Pump setpoints

The setpoint command is chosen by pump type, as in the firmware:

| Pump | Command | Register prefix | Value |
|------|---------|-----------------|-------|
| VS   | `0x01`  | `02 C4`         | BE16 RPM (450–3450) |
| VF   | `0x01`  | `02 E4`         | GPM in low byte (20–140) |
| VSF  | `0x0A` (RPM) / `0x09` (GPM) | `03 27` | BE16 RPM or GPM (by `mode`) |

### Chlorination

Chlorinator output is gated by a boolean "is a pump running" signal, mirroring
the firmware, which uses a flow-present bit rather than a numeric RPM/GPM
threshold. With no pump running, output is commanded to 0 %.

### Telemetry

The hub parses pump status (RPM / GPM / Watts, run state Stopped, Running or
Priming, and the seven drive-alarm bits, six of which carry firmware names while
the seventh is exposed as an unnamed `pump_unknown_alarm`) and IntelliChlor
status (salt ppm, alarms), and tracks per-pump comm health.

Heaters (UltraTemp / Hybrid / MasterTemp / MaxE-Therm / ETI250) and IntelliChem
are polled on their own firmware cadences, 30 s or 50 s by heater type and 30 s
for IntelliChem, independent of the pump and chlorinator loop. IntelliChem
chemistry (pH, ORP, set points, tank levels, calcium hardness, cyanuric acid,
total alkalinity, saturation index) is decoded from the `0x12` reply layout.
Gas heaters (MasterTemp, MaxE-Therm, ETI250) are read-only telemetry,
whereas the Ultra and Hybrid heat pumps also accept a mode command,
and for Hybrid a set point, which the hub packs into their poll frame. See
[caveats](#status--caveats).

### IntelliChem set-point writes

The pH and ORP set-points and the LSI inputs (calcium hardness, cyanuric acid,
total alkalinity) can be written with the `0x92` config-write (binder
`FUN_0096f3bc`, 21-byte big-endian payload, ACK reply). Because the
firmware sends all seven fields together, the library caches the last `0x12`
values and resends the untouched fields with each write, and a value is only
emitted once a status reply has been decoded. These are exposed as opt-in
`number` entities, and they control acid and chlorine dosing.

### Device presence

Like the OCP, the hub doesn't blind-scan the bus. It polls the devices you
declare in its configured routing table and infers presence from whether each
address replies: a device is marked connected when it answers a poll, and
disconnected after a few silent cycles. Pumps, chlorinators, heaters and
IntelliChem all expose this through a `comm` binary_sensor, and the routing
table is logged at boot.

### Feature circuits and heat control

The IntelliCenter OCP switches its AUX and feature relays (pool, spa, lights,
cleaner, water features, single-speed pumps, and the heater "fireman" contact)
over its own internal I2C relay hardware, keyed by feature-circuit index. The
RS-485 bus only broadcasts relay status; it never carries a "switch relay"
command. This component reproduces that with local ESP GPIO relays
(`switch:` `type: circuit`), with optional egg-timer auto-off and freeze
protection. Heating is modelled the way the OCP models it, through a body
thermostat (`climate:`) that compares water temperature against the setpoint and
mode and drives a heat-source (fireman) relay, rather than commanding the heater
over the bus. None of this touches the RS-485 segment.

## Wire framing

This component targets the **physical Pentair bus** and emits the framing that
field devices require:

- Pump / controller (A5):
  `FF 00 FF A5 <ver> <dst> <src> <cmd> <len> <data…> <ckHi> <ckLo>`
  Checksum is the 16-bit additive sum from `A5` through the last data byte.
- IntelliChlor:
  `10 02 <dst> <cmd> <data…> <ck> 10 03`
  Checksum is the low byte of the sum from `10 02` through the last data byte.

> Note: the OCP firmware writes an internal, co-processor-bound wrapper
> (`1E <seq16> <len16> …`, no checksum) to its own UARTs; a personality board
> below that layer applies the physical framing above. Since this component talks
> to real devices directly, it emits the physical framing.

## Hardware

- Any ESP32/ESP8266 supported by ESPHome, plus an RS-485 transceiver
  (MAX3485, ADM2483, etc.).
- Pentair RS-485 is **9600 baud, 8N2**.
- If your transceiver has an explicit driver-enable (DE/RE) line, wire it to a
  GPIO and set `flow_control_pin`. Auto-direction transceivers can omit it.

## Installation

Pull the component straight from GitHub (recommended):

```yaml
external_components:
  - source: github://LawPaul/ESPHome_PentairPool@v1.0.0
    components: [pentair]
```

Pinning a tag keeps your build reproducible. Use `@main` instead if you want the
latest development changes.

Or, if you have cloned the repository locally:

```yaml
external_components:
  - source:
      type: local
      path: components
```

See `example.yaml` for a complete configuration.

## Configuration reference

### `pentair:` (hub, one per bus, `MULTI_CONF`)

| Option | Default | Description |
|--------|---------|-------------|
| `uart_id` | required | UART bus to use |
| `source_address` | `0x10` | Our (master) source address |
| `flow_control_pin` | none | Optional DE/RE GPIO |
| `poll_interval` | `16s` | Idle status poll cadence (matches the IntelliCenter OCP default) |
| `active_poll_interval` | `2s` | Cadence while a pump is running or being commanded |
| `tx_gap` | `60ms` | Minimum spacing between frames |
| `require_pump_flow` | `true` | Gate chlorination on a running pump |
| `pumps` | none | List of pumps (`id`, `address`, `pump_type`, `mode`) |
| `chlorinators` | none | List of chlorinators (`id`, `address`) |
| `heaters` | none | List of heaters (`id`, `heater_type`, `address`) |
| `intellichems` | none | List of IntelliChem units (`id`, `address`) |

`heater_type` is one of `ultratemp`, `hybrid`, `mastertemp`, `max_e_therm`,
`eti250` (this selects the firmware poll cadence and request/reply command
codes). IntelliChem `address` is required: there is no firmware-fixed default,
so set it to your unit's bus address.

### Entity platforms

- `number`: `type: setpoint` (needs `pump_id`) or `type: output` (needs
  `chlorinator_id`); IntelliChem set-point writes `type: ph_setpoint |
  orp_setpoint | calcium_hardness | cyanuric_acid | total_alkalinity` (each
  needs `intellichem_id`); Hybrid heat-pump command fields `type:
  hybrid_setpoint | hybrid_boost | hybrid_param` (each needs `heater_id`)
- `switch`:
  - pump run/stop (needs `pump_id`; this is the default `type: pump`)
  - `type: circuit`, a local **feature-circuit relay** (needs `pin`; optional
    `egg_timer`, and `freeze_sensor` + `freeze_threshold` together). Not a bus
    device: it drives an ESP GPIO the way the OCP drives its I2C relays.
- `climate`: `platform: pentair`, a **body thermostat** (needs `sensor` for
  water temperature and `heat_circuit` referencing a `type: circuit` switch;
  optional `body_circuit` interlock, `hysteresis`, `visual_min_temperature`,
  `visual_max_temperature`). Supports OFF/HEAT and reports the heating action.
  Temperatures are in °C (ESPHome climate convention).
- `sensor`: `type: rpm | gpm | watts` (pump), `type: salt` (chlorinator),
  `type: heater_mode | heater_status | heater_error_a | heater_error_b |
  heater_fenwal` (heater), or `type: ph | orp | ph_setpoint | orp_setpoint |
  ph_tank | orp_tank | saturation_index | calcium_hardness | cyanuric_acid |
  total_alkalinity` (IntelliChem, needs `intellichem_id`)
- `binary_sensor`: `type: comm` (pump *or* chlorinator *or* heater *or*
  intellichem; device present / responding on the bus);
  `type: priming | pump_alarm | pump_high_temp | pump_prime_error |
  pump_over_temp | pump_power_error | pump_over_current | pump_over_voltage |
  pump_unknown_alarm` (pump, decoded from the `0x07` status reply; `pump_alarm`
  is the aggregate fault flag, the rest are the per-bit fault breakout);
  `type: flow | no_flow | low_salt | very_low_salt | clean_cell | cold_water`
  (chlorinator; alarm labels taken verbatim from the IntelliCenter firmware);
  `type: heater_fault` (heater)
- `text_sensor`: heater `type: status` (mode/status summary) or `type:
  fault_reason` (named heater faults, needs `heater_id`); IntelliChem `type:
  reply` (raw `0x12` payload hex), `alarms` / `warnings`, or `ph_dosing_status`
  / `orp_dosing_status` (each needs `intellichem_id`). Pump and chlorinator
  faults are exposed as typed `binary_sensor`s (above), not text.
- `select`: heat-pump commands over the bus (needs `heater_id`).
  `type: mode` (the default) offers Off / Heating / Cooling and applies to the
  Ultra and Hybrid heat pumps; `type: heating_mode` offers the Hybrid heat-source
  choice (Heat Pump Only / Gas Heater Only / Hybrid Mode / Dual Mode). Selecting
  an option latches the command onto that heater's next poll frame. Gas heaters
  have no proven bus command and ignore it.

## Testing

The on-wire encoders in `components/pentair/protocol.h` (physical A5 / IntelliChlor
framing, checksums, and the per-generation pump setpoint encoding) are covered by
host-side **golden-vector tests** whose expected bytes are taken directly from
the decompiled IntelliCenter OCP firmware. They build
with a plain C++17 compiler, with no ESPHome and no toolchain download:

```sh
./test/run.sh
```

Each assertion names the firmware fact it pins, so a failure means
the library has drifted from the reverse-engineered firmware behaviour. The
tests do not exercise the Python config schema, so validate the example against
real ESPHome as well:

```sh
cp secrets.yaml.example secrets.yaml
esphome config example.yaml
```

Both of the above run in CI. Runtime behaviour that depends on the ESPHome
framework (poll-cadence scheduling, presence/comm timeouts, RX field extraction)
is only exercised by a full `esphome compile` build.

## Status / caveats

- Pump status byte offsets and IntelliChlor alarm-bit meanings are decoded from
  the firmware (`IntelliFloVSF_decodeStatus07` for the pump reply, the panel's
  alert-widget label switch for the cell), not from community field maps. On
  IntelliChlor the two sources disagree: the firmware makes bit 2 "Very Low
  Salt" and clean-cell bit 4, where the njsPC wiki has bit 2 as "high salt" and
  clean-cell on bit 7. This component follows the firmware. If your equipment
  reports differently, the offsets and masks are constants near the top of
  `protocol.h`.
- Gas heaters are read-only, but heat pumps are commandable. For the gas
  heaters (MasterTemp, MaxE-Therm, ETI250) the firmware factory registers a
  status-request template but binds no request payload, unlike pumps, which
  bind a speed/flow payload, so their request bytes (and therefore any
  heat-demand command) are not reverse-engineered. The hub polls them with the
  correct command and length (payload zero-filled) and parses
  the reply (`heaterMode`, `HeaterStatus`, `ErrorFlagsA/B`, `FenwalDiag`); heat
  demand for a gas heater is instead driven by the body thermostat and fireman
  relay described below. The Ultra and Hybrid heat pumps do have a payload
  builder in the firmware, so a commanded mode (Ultra: `0x90` marker + mode;
  Hybrid: mode, heat source, set point, boost) is packed into the next poll
  frame; until you command one, the frame stays zero-filled like a gas heater.
  The reply field *order* comes straight from the decompiled parser, which
  reads its fields relative to an internal packet-buffer base (`+0x8b`) rather
  than to the on-wire payload. That base maps onto `data[0]`, which began as an
  inference but is confirmed by cross-check: the IntelliChem parser reads from
  the same base and its layout is independently verified against njsPC, so the
  offsets in `protocol.h` (`ErrorFlagsA=3`, `ErrorFlagsB=4`, `FenwalDiag=13`)
  hold. The operating-state enum is explicit in the firmware
  (`0=Off / 1=Heating / 2=Cooling`). Named faults are decoded for
  all four supported subtypes from the firmware alarm namer: the Fenwal gas
  heaters (MaxE-Therm/ETI250) from `ErrorFlagsA/B` + `FenwalDiag`, and the
  Ultra/Hybrid heat pumps from their alarm bytes at `data[8..9]` (Ultra) /
  `data[7..9]` (Hybrid). The full parser→populator→namer chain was traced, so
  the wire→label mapping is proven. Both an aggregate `heater_fault` binary
  sensor and a `heater_fault` text sensor (comma-separated alarm names) are
  exposed.
- IntelliChem chemistry is fully decoded. The 30 s poll (`0xd2` with payload
  byte `0xD2`) and its `0x12` reply were both recovered from the firmware; the
  reply is parsed by `FUN_0096fed0`, whose own debug string names every field,
  so the pH / ORP / set-point / tank / hardness / CYA / alkalinity / saturation
  offsets are established (`decode_chem_status`) and exposed as sensors. The
  raw reply hex remains available as a diagnostic text_sensor. The config-write
  (set-point) path was recovered the same way (binder `FUN_0096f3bc`, 21-byte
  big-endian payload with an ACK reply) and is emitted: the pH / ORP set-points
  and the LSI inputs (calcium hardness, cyanuric acid, total alkalinity) are
  exposed as opt-in `number` entities. Because the firmware sends all seven
  fields together, the library caches the last `0x12` values and resends the
  untouched fields with each write, and only emits once a status reply has been
  decoded.
- Relays and feature circuits are local, not a bus protocol. In the firmware
  the AUX/feature relays are switched by OCP-local I2C hardware (strings `i2c`,
  `Relay File`, `RelayStatusByCircuit`); the RS-485 bus only broadcasts relay
  *status* to remote panels, never actuation. So `type: circuit` switches and
  the `climate:` thermostat drive the ESP's own GPIO and do not emit any Pentair
  frame. The variable-speed pump and IntelliChlor are addressed over the bus
  (they are not relay loads, and a relay cannot set RPM or cell output %).
- Heat control is a thermostat plus a relay, not a gas-heater command. Matching
  the firmware, the body thermostat compares water temperature (a local ESPHome
  sensor, since the OCP reads its own analog probe rather than the bus) to the
  setpoint and energises a heat-source (fireman) relay circuit, with an optional
  body-circuit interlock ("No Body Circuit is Running! Can't Process Heater
  Change!"). It does
  not send heat demand to a gas heater over RS-485, because the firmware doesn't
  either (see the gas-heater note above). Ultra / Hybrid heat pumps are the
  exception: they take a mode command on the bus via the `select:` platform.
- Hardware-validated on an **IntelliFlo3**: the A5 framing, remote-control
  keepalive, setpoint writes and `0x07` status decode all work against a real
  pump.

## Prior work and credits

This component's protocol facts come from the decompiled firmware, but several
were checked against [nodejs-poolController][njspc] (njsPC) by Russell Goldin
([@tagyoureit][tag]) and its contributors, the long-running open-source Pentair
project. Those comparisons are cited inline at each site, and they are the
reason some offsets are stated with confidence instead of as inferences:

- The IntelliChem `0x12` payload base is njsPC-verified, which is what anchors
  the heater field offsets (see [caveats](#status--caveats)).
- The IntelliChem alarm bit 7 (Probe Fault) is taken from njsPC's byte map; the
  firmware only bit-tests bits 0-6.
- The dosing-status nibble decode and the VF `setPumpFeature(6)` gate are
  confirmed by njsPC's behavior.
- Two facts go the other way: the firmware disagrees with the njsPC wiki on
  IntelliChlor alarm bit 2, and supplies the hybrid `economyTime` range that
  njsPC hard-defaults.

njsPC is licensed **AGPL-3.0**; this component is MIT. No njsPC code was copied
here. What was used is factual protocol information (byte offsets and field
meanings) rather than any part of its implementation.

njsPC in turn credits the earlier decoding work of Jason Young, Michael Russe
and Michael Usner, which this component also stands on indirectly.

[njspc]: https://github.com/tagyoureit/nodejs-poolController
[tag]: https://github.com/tagyoureit

## License

MIT. See [`LICENSE`](LICENSE).

## Disclaimer

This project is an independent, community effort and is **not affiliated with,
endorsed by, or supported by Pentair**. "Pentair", "IntelliCenter",
"IntelliFlo", "IntelliChlor", "IntelliChem", "UltraTemp", "MasterTemp" and
"MaxE-Therm" are trademarks of Pentair plc and/or its subsidiaries, used here
only to identify the equipment this component interoperates with.

The RS-485 protocol details were obtained by reverse engineering publicly
obtainable firmware for the purpose of interoperability. No Pentair
confidential information, source code or documentation was used, and no Pentair
code is included in this repository. Use at your own risk:
commanding pool equipment incorrectly can damage hardware or create unsafe
conditions. Always validate against your own equipment before relying on this
for automation.
