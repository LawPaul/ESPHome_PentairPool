#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace esphome {
namespace pentair {

// ---------------------------------------------------------------------------
// Physical Pentair RS-485 bus definitions.
//
// The IntelliCenter OCP firmware itself writes an internal, co-processor-bound
// wrapper (`1E <seq16> <len16> ...`, no checksum) to /dev/ttyO1/2; a personality
// board below that layer applies the real physical framing. This library talks
// to real field devices directly, so it emits the physical framing that VS/VSF/
// VF pumps and IntelliChlor cells actually require:
//
//   Pump / controller (A5): FF 00 FF A5 <ver> <dst> <src> <cmd> <len> <data...>
//                           <ckHi> <ckLo>
//     checksum = 16-bit sum of every byte from A5 through the last data byte.
//
//   IntelliChlor (10/02):   10 02 <dst> <cmd> <data...> <ck> 10 03
//     checksum = low byte of the sum from 10 02 through the last data byte.
//
// The pump command/register semantics (cmd 1 vs 9/10, prefixes 02 C4 / 02 E4 /
// 03 27) and the boolean flow-gate for chlorination are taken from the firmware
// analysis (see the "Firmware provenance & protocol overview" section in README.md).
// ---------------------------------------------------------------------------

// Well-known bus addresses.
static const uint8_t ADDR_BROADCAST = 0x0F;
static const uint8_t ADDR_CONTROLLER_DEFAULT = 0x10;  // our (master) source address
static const uint8_t ADDR_PUMP_BASE = 0x60;           // pump N -> 0x60 + (N-1)
static const uint8_t ADDR_CHLOR_DEFAULT = 0x50;       // IntelliChlor

// A5 frame markers.
static const uint8_t A5_PREAMBLE_0 = 0xFF;
static const uint8_t A5_PREAMBLE_1 = 0x00;
static const uint8_t A5_PREAMBLE_2 = 0xFF;
static const uint8_t A5_START = 0xA5;
static const uint8_t A5_VERSION = 0x00;

// IntelliChlor DLE framing.
static const uint8_t CHLOR_DLE = 0x10;
static const uint8_t CHLOR_STX = 0x02;
static const uint8_t CHLOR_ETX = 0x03;

// Pump commands (IntelliFlo).
static const uint8_t PUMP_CMD_SETPOINT = 0x01;      // VS / VF register write
static const uint8_t PUMP_CMD_REMOTE = 0x04;        // HandOn/HandOff: 0xFF hold remote, 0x00 release
static const uint8_t PUMP_CMD_RUN = 0x06;           // 0x0A = run, 0x04 = stop
static const uint8_t PUMP_CMD_STATUS = 0x07;        // request / reply
static const uint8_t PUMP_CMD_SETPOINT_VSF = 0x09;       // VSF Flow  (GPM), firmware "VSF Flow"
static const uint8_t PUMP_CMD_SETPOINT_VSF_SPEED = 0x0A;  // VSF Speed (RPM), firmware "VSF Speed"
static const uint8_t PUMP_CMD_FEATURE = 0x05;       // VF feature/menu select
static const uint8_t PUMP_FEATURE_1 = 0x06;         // cmd 5 payload (Feature 1)

// cmd 4 (HandOn/HandOff) payload. The OCP re-asserts 0xFF on every active poll
// cycle to hold the pump under remote control; the pump reverts to its local
// program if this stops arriving (see the protocol overview in README.md).
static const uint8_t PUMP_REMOTE_ON = 0xFF;
static const uint8_t PUMP_REMOTE_OFF = 0x00;
static const uint8_t PUMP_RUN = 0x0A;
static const uint8_t PUMP_STOP = 0x04;

// ---- Pump status (cmd 0x07) reply decode ------------------------------------
// [E] IntelliFloVSF_decodeStatus07 (FUN_0096cc58) + its logger
// IntelliFloVSF_logStatusPacket (FUN_0096c514) establish the reply layout and
// the run-state / alarm-bit semantics (data index = reply payload byte):
//   d[0]     run byte: 0x0A -> running (else STOPPED)
//   d[2]     drive sub-state: == 1 while running -> PRIMING (else RUNNING)
//   d[3..4]  WATTS (BE16)      d[5..6]  RPM (BE16)      d[7..8]  GPM (u16 LE)
//   d[9..10] alarm word (BE16, d[9] hi): named bits below
// (The firmware logs "STOPPED / RUNNING / PRIMING" for d[0]/d[2], and the seven
//  alarm bits below by name.)
enum PumpRunState: uint8_t {
  PUMP_STATE_STOPPED = 0,
  PUMP_STATE_RUNNING = 1,
  PUMP_STATE_PRIMING = 2,
};

// Alarm masks on the BE16 word W = (d[9] << 8) | d[10] (firmware bit names).
static const uint16_t PUMP_ALARM_HIGH_TEMP = 1u << 2;
static const uint16_t PUMP_ALARM_PRIME = 1u << 3;
static const uint16_t PUMP_ALARM_UNKNOWN = 1u << 5;
static const uint16_t PUMP_ALARM_OVER_TEMP = 1u << 6;
static const uint16_t PUMP_ALARM_POWER = 1u << 7;
static const uint16_t PUMP_ALARM_OVER_CURRENT = 1u << 8;
static const uint16_t PUMP_ALARM_OVER_VOLTAGE = 1u << 9;
static const uint16_t PUMP_ALARM_ANY =
    PUMP_ALARM_HIGH_TEMP | PUMP_ALARM_PRIME | PUMP_ALARM_UNKNOWN |
    PUMP_ALARM_OVER_TEMP | PUMP_ALARM_POWER | PUMP_ALARM_OVER_CURRENT |
    PUMP_ALARM_OVER_VOLTAGE;

struct PumpStatus {
  PumpRunState state{PUMP_STATE_STOPPED};
  uint16_t watts{0};
  uint16_t rpm{0};
  uint8_t gpm{0};
  uint16_t alarms{0};  // masked with PUMP_ALARM_* bits
};

// Pure decoder (host-testable). Fields default to 0 for short payloads; the
// alarm word is only read when the payload is long enough to carry it.
inline bool decode_pump_status(const uint8_t *d, size_t len, PumpStatus &out) {
  if (d == nullptr || len == 0)
    return false;
  bool run = d[0] == PUMP_RUN;
  bool priming = run && len > 2 && d[2] == 1;
  out.state = priming ? PUMP_STATE_PRIMING
                      : (run ? PUMP_STATE_RUNNING: PUMP_STATE_STOPPED);
  out.watts = len >= 5 ? static_cast<uint16_t>((d[3] << 8) | d[4]): 0;
  out.rpm = len >= 7 ? static_cast<uint16_t>((d[5] << 8) | d[6]): 0;
  out.gpm = len >= 8 ? d[7]: 0;
  out.alarms = len >= 11 ? static_cast<uint16_t>(((d[9] << 8) | d[10]) & PUMP_ALARM_ANY): 0;
  return true;
}

// Setpoint register prefixes (firmware-derived).
static const uint8_t REG_VS_HI = 0x02, REG_VS_LO = 0xC4;    // VS  -> BE16 RPM
static const uint8_t REG_VF_HI = 0x02, REG_VF_LO = 0xE4;    // VF  -> BE16 GPM
static const uint8_t REG_VSF_HI = 0x03, REG_VSF_LO = 0x27;  // VSF -> BE16 RPM/GPM

// IntelliChlor commands.
static const uint8_t CHLOR_CMD_SET_OUTPUT = 0x11;  // data: [output %]
static const uint8_t CHLOR_CMD_STATUS = 0x12;      // reply: [salt, alarms, ...]

// IntelliChlor cmd 0x12 status decode. Salt ppm = raw * 50 (firmware-derived,
// confirmed by real captures: 0x3A -> 2900 ppm). Alarm bits are decoded by the
// firmware status consumer FUN_006f5b94, which bit-tests bits 0,1,2,4,6 ->
// no-flow / low-salt / very-low-salt / inspect-cell / cold-water (labels are
// firmware-authoritative, resolved from the paintEvent label switch). NOTE: the
// tagyoureit/nodejs-poolController wiki mislabels bit 2 (0x04) as "high salt"
// and keys clean-cell off bit 7; the IntelliCenter firmware disagrees -- bit 2
// is "Very Low Salt" (there is no high-salt condition) and clean-cell is bit 4.
// A captured composite 0x90 = bit 4 "Inspect Cell" + bit 7 (read, never surfaced).
static const uint16_t CHLOR_SALT_SCALE = 50;
// Alarm bit -> code -> on-screen label, decoded verbatim from the IntelliCenter
// firmware (consumer FUN_006f5b94 bit test; label switch in the alert-widget
// paintEvent FUN_0081a8e8). The panel tests exactly bits 0,1,2,4,6; bits 3,5,7
// are read but never surfaced. These labels are firmware-authoritative [E].
static const uint8_t CHLOR_ALARM_NO_FLOW = 0x01;        // bit 0 -> "No Flow"
static const uint8_t CHLOR_ALARM_LOW_SALT = 0x02;       // bit 1 -> "Low Salt"
static const uint8_t CHLOR_ALARM_VERY_LOW_SALT = 0x04;  // bit 2 -> "Very Low Salt"
static const uint8_t CHLOR_ALARM_CLEAN_CELL = 0x10;     // bit 4 -> "Inspect Cell"
static const uint8_t CHLOR_ALARM_COLD_WATER = 0x40;     // bit 6 -> "Cold Water Cutoff"

// ---- Heaters (A5 bus) -------------------------------------------------
// The OCP polls each heater on a fixed cadence and parses a status reply. The
// GAS heaters' status-request payload is NOT decoded in firmware (their setup
// binds no payload), so they are polled with a zero-filled payload and treated
// as read-only telemetry. The Ultra and Hybrid heat pumps DO have a firmware
// payload builder, so a commanded mode (and for Hybrid a set point) is packed
// into their poll frame; see build_ultra_cmd_payload / build_hybrid_cmd_payload
// and heater_supports_bus_command() below.
//
// Per-subtype request/response command codes and cadence (firmware factory
// FUN_009765d8):
//   subtype 4 Ultra: poll 30s, req 0x72 (len 10) -> reply 0x73
//   subtype 5 Hybrid: poll 50s, req 0x70 (len 10) -> reply 0x71
//   subtype 6 MasterTemp: poll 50s, req 0x70 (len 10) -> reply 0x74
//   subtype 7 MaxETherm: poll 50s, req 0x70 (len 10) -> reply 0x74
//   subtype 8 ETI250: poll 50s, req 0x80 (len 12) -> reply 0x81
enum HeaterType: uint8_t {
  HEATER_TYPE_ULTRA = 4,
  HEATER_TYPE_HYBRID = 5,
  HEATER_TYPE_MASTERTEMP = 6,
  HEATER_TYPE_MAXETHERM = 7,
  HEATER_TYPE_ETI250 = 8,
};

// Heater status-request command + declared payload length by subtype.
inline uint8_t heater_request_cmd(HeaterType type) {
  switch (type) {
    case HEATER_TYPE_ULTRA:
      return 0x72;
    case HEATER_TYPE_ETI250:
      return 0x80;
    default:  // Hybrid / MasterTemp / MaxETherm
      return 0x70;
  }
}

inline uint8_t heater_request_len(HeaterType type) {
  return type == HEATER_TYPE_ETI250 ? 12: 10;
}

// A reply cmd that belongs to any heater subtype. Used to route RX frames.
inline bool is_heater_reply_cmd(uint8_t cmd) {
  return cmd == 0x71 || cmd == 0x73 || cmd == 0x74 || cmd == 0x81;
}

// Firmware poll cadence per subtype, milliseconds.
inline uint32_t heater_poll_interval_ms(HeaterType type) {
  return type == HEATER_TYPE_ULTRA ? 30000u: 50000u;
}

// Heater status-reply field offsets within the A5 data payload.
//
// [E] The MasterTemp parser (FUN_00977c30) reads a contiguous record; the field
// ORDER and relative spacing are firmware-proven (leading, heaterMode,
// HeaterStatus, ErrorFlagsA, ErrorFlagsB, two u32 LE, FenwalDiag). Mapping the
// parser's packet-buffer base (0x8b) to data[0] of the on-wire payload is now
// confirmed via cross-check: the IntelliChem parser shares the same base
// (message +0x8b -> data[0]) and its layout is njsPC-verified, so the heater
// byte offsets below (ErrorFlagsA=3, ErrorFlagsB=4, FenwalDiag=13) are solid.
// The per-subtype MEANING of the ErrorFlagsA/B bits and the FenwalDiag nibble
// is decoded by heater_fault_string() for the gas heaters (MaxE-Therm, ETI250)
// and by heat_pump_fault_string() for the heat pumps (Ultra, Hybrid).
static const uint8_t HEATER_OFF_MODE = 1;    // heaterMode
static const uint8_t HEATER_OFF_STATUS = 2;  // HeaterStatus
static const uint8_t HEATER_OFF_ERR_A = 3;   // ErrorFlagsA  (gas heaters)
static const uint8_t HEATER_OFF_ERR_B = 4;   // ErrorFlagsB  (gas heaters)
static const uint8_t HEATER_OFF_FENWAL = 13;  // FenwalDiag  (gas heaters)

// Heat-pump alarm byte offsets in the A5 data payload. Proven from
// firmware v3.008: the Ultra parser (FUN_0097e234) reads wire d8,d9 and the
// Hybrid parser (FUN_0097c300) reads wire d7,d8,d9 as the alarm field.
static const uint8_t HEATER_OFF_HP_D7 = 7;   // Hybrid alarm byte 0
static const uint8_t HEATER_OFF_HP_D8 = 8;   // Ultra alarm byte 0 / Hybrid byte 1
static const uint8_t HEATER_OFF_HP_D9 = 9;   // Ultra alarm byte 1 / Hybrid byte 2

// Named fault decode for the Fenwal-based GAS heaters (MaxE-Therm, ETI250).
//
// [E] Bit meanings come from the firmware alarm/notification namer
// (FUN_008c1dfc), read from its bit-test control flow per subtype. ETI250 and
// MaxE-Therm share the wire layout (ErrorFlagsA=data[3], ErrorFlagsB=data[4],
// FenwalDiag=data[13]) but differ in which bits they populate. The FenwalDiag
// low nibble is a shared ignition-controller diagnostic enum.
//
// Heat-pump subtypes (Ultra / Hybrid) are decoded by heat_pump_fault_string()
// below (different wire bytes); this function returns "" for them.
inline std::string heater_fault_string(HeaterType type, uint8_t a, uint8_t b, uint8_t fenwal) {
  std::string s;
  auto add = [&](const char *n) {
    if (!s.empty())
      s += ", ";
    s += n;
  };
  if (type == HEATER_TYPE_ETI250) {
    if (a & 0x01)
      add("Water Pressure Switch");
    if (a & 0x02)
      add("High Limit Switch");
    if (a & 0x04)
      add("Air Flow Switch");
    if (a & 0x08)
      add("Auto Gas Shutoff Switch");
    if (a & 0x10)
      add("Ignition Control Value Signal");
    if (a & 0x20)
      add("Stack Flue Sensor Error");
    if (a & 0x40)
      add("Thermal Fuse Switch Error");
    if (a & 0x80)
      add("Condensate Float Switch Error");
    if (b & 0x01)
      add("Stack Flue Sensor Open");
    if (b & 0x02)
      add("Stack Flue Sensor Short");
    if (b & 0x04)
      add("Water Sensor Open");
    if (b & 0x08)
      add("Water Sensor Short");
    if (b & 0x10)
      add("Replace Condensate Cartridge");
  } else if (type == HEATER_TYPE_MAXETHERM) {
    if (a & 0x01)
      add("Water Pressure Switch");
    if (a & 0x02)
      add("High Limit Switch");
    if (a & 0x04)
      add("Air Flow Switch");
    if (a & 0x08)
      add("Auto Gas Shutoff Switch");
    if (a & 0x10)
      add("Ignition Control Error");
    if (a & 0x20)
      add("Stack Flue Sensor Error");
    if (b & 0x01)
      add("Stack Flue Sensor Open");
    if (b & 0x02)
      add("Stack Flue Sensor Short");
    if (b & 0x04)
      add("Water Sensor Open");
    if (b & 0x08)
      add("Water Sensor Short");
  } else {
    return s;  // heat-pump subtypes handled by heat_pump_fault_string()
  }
  // Shared Fenwal ignition-controller diagnostic (data[13] low nibble).
  switch (fenwal & 0x0f) {
    case 1:
      add("Air Flow Fault");
      break;
    case 2:
      add("Flame No Call For Heat");
      break;
    case 3:
      add("Ignition Lockout");
      break;
    case 4:
      add("Weak Flame");
      break;
    default:
      break;
  }
  return s;
}

// True for the two heat-pump subtypes whose alarm bytes are at data[7..9].
inline bool heater_is_heat_pump(HeaterType type) {
  return type == HEATER_TYPE_ULTRA || type == HEATER_TYPE_HYBRID;
}

// Named fault decode for the HEAT-PUMP subtypes (Ultra, Hybrid).
//
// [E] Proven end-to-end from firmware v3.008 (payload base +0x8b -> data[0]):
//   1. Parsers store the wire alarm bytes into the device status blob
//      (DataModel token 0xede0):
//        Ultra  FUN_0097e234: U32LE(d8,d9,0,0)      -> blob[3]=d8, blob[4]=d9
//        Hybrid FUN_0097c300: U32LE(d7,d8,d9,0)     -> blob[3]=d7, blob[4]=d8,
//                                                       blob[5]=d9, blob[6]=0
//   2. Aggregate populator FUN_008c8ea4 copies, keyed on subtype token 0xef04:
//        subtype 4 (Ultra)  -> 16-bit word [B6=blob[3]=d8, B7=blob[4]=d9]
//        subtype 5 (Hybrid) -> 24-bit field [byte0=blob[3]=d7, byte1=blob[4]=d8,
//                                             byte2=blob[5]=d9]
//   3. Namer FUN_008c1dfc bit-tests that word/field; each bit maps to a firmware
//      alarm string (resolved from the binary). So Ultra faults live in wire
//      d8/d9 and Hybrid faults in wire d7/d8/d9. The strings below are the
//      firmware's own labels.
inline std::string heat_pump_fault_string(HeaterType type, uint8_t d7, uint8_t d8, uint8_t d9) {
  std::string s;
  auto add = [&](const char *n) {
    if (!s.empty())
      s += ", ";
    s += n;
  };
  if (type == HEATER_TYPE_ULTRA) {
    // d8 (aggregate B6)
    if (d8 & 0x01)
      add("Brownout");
    if (d8 & 0x02)
      add("High Refrigerant");
    if (d8 & 0x04)
      add("Low Refrigerant");
    if (d8 & 0x08)
      add("5 Alarms in an hour");
    if (d8 & 0x10)
      add("Low Ambient Temperature");
    // d9 (aggregate B7)
    if (d9 & 0x01)
      add("High Water Temperature");
    if (d9 & 0x02)
      add("Low Water Temperature");
    if (d9 & 0x04)
      add("Low Water Flow");
    if (d9 & 0x08)
      add("Pool and Spa remote inputs are both enabled");
    if (d9 & 0x10)
      add("Water Temp Sensor Open");
    if (d9 & 0x20)
      add("Water Temp Sensor Shorted");
    if (d9 & 0x40)
      add("Defrost Temp Sensor Open");
    if (d9 & 0x80)
      add("Defrost Temp Sensor Shorted");
  } else if (type == HEATER_TYPE_HYBRID) {
    // d7 (aggregate byte 0)
    if (d7 & 0x01)
      add("Air Flow Switch");
    if (d7 & 0x02)
      add("ICM Fault");
    if (d7 & 0x04)
      add("Automatic Gas Shut Off");
    if (d7 & 0x08)
      add("Stack Flue High Temp");
    if (d7 & 0x10)
      add("Stack Flue Open/Short");
    if (d7 & 0x20)
      add("Stack Flue Runaway");
    if (d7 & 0x40)
      add("Freeze Warning");
    if (d7 & 0x80)
      add("Condensate Filter");
    // d8 (aggregate byte 1)
    if (d8 & 0x01)
      add("Brownout");
    if (d8 & 0x02)
      add("High Refrigerant");
    if (d8 & 0x04)
      add("Low Refrigerant");
    if (d8 & 0x08)
      add("5 Alarms in an hour");
    if (d8 & 0x10)
      add("Low Ambient Temperature");
    if (d8 & 0x20)
      add("Condensate Float Switch");
    if (d8 & 0x40)
      add("Thermal Fuse");
    if (d8 & 0x80)
      add("High Limit Switch");
    // d9 (aggregate byte 2)
    if (d9 & 0x01)
      add("High Water Temperature");
    if (d9 & 0x02)
      add("Low Water Temperature");
    if (d9 & 0x04)
      add("Low Water Flow");
    if (d9 & 0x08)
      add("Water Temperature Sensor Open/Short");
    if (d9 & 0x10)
      add("Suction Temperature Sensor Open/Short");
  }
  return s;
}

// True when this subtype's faults are decoded to names (gas or heat pump).
inline bool heater_faults_named(HeaterType type) {
  return type == HEATER_TYPE_ETI250 || type == HEATER_TYPE_MAXETHERM ||
         type == HEATER_TYPE_ULTRA || type == HEATER_TYPE_HYBRID;
}

// ---- Heat-pump command ------------------------------
// [E] Proven from firmware v3.008. Unlike the gas heaters, the Ultra/Hybrid
// setup binds a payload builder that packs a command into the SAME poll frame
// (Ultra cmd 0x72 / Hybrid cmd 0x70, declared len 10). The two subtypes use
// DIFFERENT payload layouts (re-verified 3e23 -- they are NOT interchangeable):
//
//   ULTRA  HeaterUltra_setupMessages @0x0097de9c -> setUltraParams FUN_0097d428
//     payload[0] = 0x90                      (message +5,  fixed marker)
//     payload[1] = mode (0 Off / 1 Heating / 2 Cooling, token 0xeda8; forced 0
//                        when service-gate FUN_0068b9f4 fires)
//     payload[9] = service (message +0xe; set only in system service mode)
//     Firmware Tx log: "[%s] addr( 0x%x) mode( %d) service( %d)".
//     -> fully proven and emitted (build_ultra_cmd_payload below).
//
//   HYBRID HeaterHybrid_setupMessages @0x0097bfac -> FUN_0097b558 (cmd 0x70,
//   reply 0x71, declared len 10). Re-derived byte-for-byte from the firmware
//   builder (3e26); it writes into the SAME poll frame (payload[n] = msg+5+n):
//     payload[0] = mode        (token 0xeda8, 0 Off / 1 Heating / 2 Cooling)
//                  <- NO 0x90 marker (that is where the Ultra puts its marker)
//     payload[1] = heatingMode (token 0xa5d6): 1 "Heat Pump Only" /
//                  2 "Gas Heater Only" / 3 "Hybrid Mode" / 4 "Dual Mode"
//                  (enum proven via the log formatter FUN_009666a0)
//     payload[2] = Set Point   (active body's heat setpoint, default 0x4e=78)
//     payload[3] = 0xf38c      (settable byte; setter FUN_00687da4 clamps to
//                  [5,60], class init default 0x0f=15; written to the wire but
//                  the ONLY field the Tx log omits -- we emit the default 15)
//                  IDENTITY (3e28, firmware-confirmed): token 0xf38c's datamodel
//                  property is owned by RTTI class "DelaysObject" (its getter
//                  FUN_006883b4 / setter FUN_00687da4 store it at obj+0x3a and
//                  clamp [5,60]) -- i.e. it is a DELAY/TIMER value in minutes,
//                  NOT a temperature. The firmware also retains (un-stripped in
//.dynsym) the shared-memory-constant symbols for the hybrid
//                  config triplet: SMConstants::MAPSET_hybrid_efficiency_mode
//                  (@0xbcbfc8),::MAPSET_hybrid_boost_temp (@0xbcbfc4) and
//::MAPSET_hybrid_eco_time (@0xbcbfc0) -- three consecutive
//                  offsets (3684/3700/3716). That named triplet maps 1:1 onto
//                  this command's wire triplet {heatingMode=payload[1],
//                  boostTemp=payload[4], <this field>=payload[3]}, so payload[3]
//                  is the hybrid "eco_time" = ECONOMY TIME (minutes). This
//                  matches the njsPC config field "economyTime" (Action 168 cat
//                  10, b29), the one value njsPC could not source ("need to come
//                  from somewhere", hard-defaulted to 1); the firmware's DelaysO-
//                  bject [5,60]/default-15 supplies the range/units it lacked.
//                  The only step not proven by a single instruction is the
//                  token(0xf38c)<->eco_time-offset(3716) join (different SM
//                  mirrors), but the DelaysObject timer type + triplet position
//                  make "economy time" firmware-corroborated, not a guess. We
//                  still ship the firmware-proven faithful default 15 as a
//                  tunable [5,60] byte. The 2009 sdyoung/Lafleur RE predates the
//                  UltraTemp ETi Hybrid entirely and has NO coverage of this cmd.
//     payload[4] = boostTemp   (token 0x8bc8)
//     payload[9] = service     (global service-mode flag *(FUN_007413ec()+0xc0),
//                  same as the Ultra; 0 in normal operation)
//     Firmware Tx log: "addr() mode() Set Point() service() boostTemp()
//     heating Mode()" (payload[3]/0xf38c is not logged).
//
//   DESIGN (3e26): the firmware builder READS every field back from the OCP's
//   own datamodel because it IS the OCP. This library is a full IntelliCenter /
//   OCP replacement -- the ESP owns the body/heat state in its own entities and
//   is the sole bus master -- so it builds the Hybrid frame from those entities
//   directly (Set Point from the body climate, mode/heatingMode/boostTemp from
//   the heater entities, payload[3] a tunable [5,60] defaulting to the
//   firmware's 15). Deployment assumes NO live IntelliCenter on the bus (a
//   second master would collide). See build_hybrid_cmd_payload /
//   heater_supports_bus_command below.
static const uint8_t ULTRA_CMD_MARKER = 0x90;  // payload[0]
static const uint8_t ULTRA_CMD_OFF_MARKER = 0;   // payload index of marker
static const uint8_t ULTRA_CMD_OFF_MODE = 1;     // payload index of mode
static const uint8_t ULTRA_CMD_OFF_SERVICE = 9;  // payload index of service

enum HeatPumpMode: uint8_t {
  HEAT_PUMP_MODE_OFF = 0,      // firmware HEATER_STATUS_OFF
  HEAT_PUMP_MODE_HEATING = 1,  // firmware HEATER_STATUS_HEATING
  HEAT_PUMP_MODE_COOLING = 2,  // firmware HEATER_STATUS_COOLING
};

inline const char *heat_pump_mode_name(HeatPumpMode mode) {
  switch (mode) {
    case HEAT_PUMP_MODE_HEATING:
      return "Heating";
    case HEAT_PUMP_MODE_COOLING:
      return "Cooling";
    default:
      return "Off";
  }
}

// True if this subtype's poll frame doubles as a proven bus command that the
// library emits. The Ultra and Hybrid heat pumps both carry a firmware-proven
// command payload (build_ultra_cmd_payload / build_hybrid_cmd_payload); as the
// OCP replacement / sole bus master the library drives them from its own
// entities. Gas heaters have no bus command (their poll is status-only).
inline bool heater_supports_bus_command(HeaterType type) {
  return type == HEATER_TYPE_ULTRA || type == HEATER_TYPE_HYBRID;
}

// Pack the Ultra command payload, mirroring setUltraParams (FUN_0097d428):
// payload[0]=0x90 marker, payload[1]=mode, payload[9]=service, rest zero. out
// must point at heater_request_len() (10) bytes.
inline void build_ultra_cmd_payload(HeatPumpMode mode, bool service, uint8_t *out, uint8_t len) {
  for (uint8_t i = 0; i < len; i++)
    out[i] = 0;
  out[ULTRA_CMD_OFF_MARKER] = ULTRA_CMD_MARKER;
  out[ULTRA_CMD_OFF_MODE] = static_cast<uint8_t>(mode);
  if (len > ULTRA_CMD_OFF_SERVICE)
    out[ULTRA_CMD_OFF_SERVICE] = service ? 1: 0;
}

// ---- Hybrid command --------------------------------------
// Layout re-derived from FUN_0097b558 (see the comment block above). Unlike the
// Ultra there is NO 0x90 marker: mode / heatingMode / Set Point / <0xf38c> /
// boostTemp pack into payload[0..4] and service into payload[9].
static const uint8_t HYBRID_CMD_OFF_MODE = 0;      // payload[0]
static const uint8_t HYBRID_CMD_OFF_HEATMODE = 1;  // payload[1]
static const uint8_t HYBRID_CMD_OFF_SETPOINT = 2;  // payload[2]
static const uint8_t HYBRID_CMD_OFF_PARAM = 3;     // payload[3] token 0xf38c = eco_time (min)
static const uint8_t HYBRID_CMD_OFF_BOOST = 4;     // payload[4]
static const uint8_t HYBRID_CMD_OFF_SERVICE = 9;   // payload[9]

// payload[3] (token 0xf38c = hybrid "eco_time", economy time in minutes; a
// DelaysObject property -- see IDENTITY note above): firmware clamps to [5,60];
// class init default 15.
static const uint8_t HYBRID_PARAM_MIN = 5;
static const uint8_t HYBRID_PARAM_MAX = 60;
static const uint8_t HYBRID_PARAM_DEFAULT = 15;
// Set Point falls back to 0x4e (78) in firmware when no body setpoint is known.
static const uint8_t HYBRID_SETPOINT_DEFAULT = 0x4e;

// Heat-source selection (token 0xa5d6), proven via log formatter FUN_009666a0.
enum HybridHeatingMode: uint8_t {
  HYBRID_HEAT_UNSET = 0,      // firmware default / empty
  HYBRID_HEAT_PUMP_ONLY = 1,  // "Heat Pump Only"
  HYBRID_HEAT_GAS_ONLY = 2,   // "Gas Heater Only"
  HYBRID_HEAT_HYBRID = 3,     // "Hybrid Mode"
  HYBRID_HEAT_DUAL = 4,       // "Dual Mode"
};

inline const char *hybrid_heating_mode_name(HybridHeatingMode m) {
  switch (m) {
    case HYBRID_HEAT_PUMP_ONLY:
      return "Heat Pump Only";
    case HYBRID_HEAT_GAS_ONLY:
      return "Gas Heater Only";
    case HYBRID_HEAT_HYBRID:
      return "Hybrid Mode";
    case HYBRID_HEAT_DUAL:
      return "Dual Mode";
    default:
      return "Unset";
  }
}

// Pack the Hybrid command payload, mirroring FUN_0097b558. out must point at
// heater_request_len() (10) bytes; param is the payload[3]/0xf38c byte and is
// clamped to [5,60] exactly as the firmware setter FUN_00687da4 does.
inline void build_hybrid_cmd_payload(HeatPumpMode mode, HybridHeatingMode heating_mode,
                                     uint8_t set_point, uint8_t param, uint8_t boost_temp,
                                     bool service, uint8_t *out, uint8_t len) {
  for (uint8_t i = 0; i < len; i++)
    out[i] = 0;
  if (param < HYBRID_PARAM_MIN)
    param = HYBRID_PARAM_MIN;
  else if (param > HYBRID_PARAM_MAX)
    param = HYBRID_PARAM_MAX;
  out[HYBRID_CMD_OFF_MODE] = static_cast<uint8_t>(mode);
  out[HYBRID_CMD_OFF_HEATMODE] = static_cast<uint8_t>(heating_mode);
  out[HYBRID_CMD_OFF_SETPOINT] = set_point;
  out[HYBRID_CMD_OFF_PARAM] = param;
  out[HYBRID_CMD_OFF_BOOST] = boost_temp;
  if (len > HYBRID_CMD_OFF_SERVICE)
    out[HYBRID_CMD_OFF_SERVICE] = service ? 1: 0;
}

// ---- KeepAlive / master status broadcast -----------------
// [E] RE'd from firmware, DELIBERATELY NOT EMITTED. This block documents the
// full proven wire layout so it is not lost; there is intentionally no builder.
//
// The KeepAlive_comm class (setup FUN_0096a7b8) is the OCP master's periodic
// controller-status broadcast. It only runs when this unit is the bus master
// (gate FUN_007337f4 == 3) and registers TWO templates, both A5, dst broadcast,
// re-sent on a 2s cadence:
//
//   * cmd 0x02 "KA Legacy"      len 0x1d = 29   builder FUN_00968f84
//   * cmd 0xcc "KA Intellicenter" len 0x30 = 48 builder FUN_00969dbc
//
// These are NOT a small "time + heat-setpoint" frame. Each builder reads live
// state from essentially the ENTIRE OCP datamodel and packs the whole
// controller status: clock, feature-circuit on/off bitmap, heat modes/sources,
// multiple water temperatures, valve/body state, comm-lost bitmask, schedule
// countdown, and (for 0xcc) the panel-discovery handshake.
//
// cmd 0x02 payload map (payload[n] = msg struct + 5 + n), builder FUN_00968f84:
//   p0  = clock hour           (QTime::hour)
//   p1  = clock minute         (QTime::minute)
//   p2..p8 = feature-circuit on/off bitmap, 44 circuits @ 8/byte
//            (FUN_00968d94; p7 low nibble is circuits, p7 high nibble + p8 are
//             overwritten with heat-source selection nibbles below)
//   p7  high nibble / p8 = heat-source selection nibbles
//            (FUN_00968b70 body-A, FUN_00968c64 body-B)
//   p9  = heat-mode/source flags byte
//            base 0x20 | (service&3) | 0x04(circuit tok 0xba74) |
//            0x08(spa active) | 0x80(tok via FUN_007f2b68); sys-mode dependent
//   p10 = per-body heat-source select (tokens 0xa5d6 heatingMode / 0xef04 type;
//            body-type 0xea7c = spa branch)
//   p11 = combined heat-mode nibbles ((char)uVar6)
//   p12 = body/valve active bits (bit0 body0, bit1 body1)
//   p13 = 0
//   p14 = water temp (FUN_007d5914 idx 0)
//   p15 = water temp (idx 0 or idx 3 when sys-mode==3)
//   p16 = heat-mode flags | (heat-source-B nibble >> 4)
//   p17 = water temp (idx 4)
//   p18 = water temp (idx 2)
//   p19 = water temp (idx 1)
//   p20 = temp (FUN_007d70b4, air/solar)
//   p21 = water temp (idx 3)
//   p22 = 0
//   p23 = 0
//   p24 = token 0xbc0a (u8)
//   p25 = 0
//   p26 = clock second
//   p27 = FUN_0084d414 (u8)
//   p28 = sys mode (FUN_0073e300: 2=shared, 3=separate)
//
// cmd 0xcc payload map (payload[n] = msg struct + 5 + n), builder FUN_00969dbc:
//   p0  = high byte of FUN_007ddef8()      p1 = low byte
//   p2  = FUN_0098f02c-derived u16 low     p3 = its high byte (iVar6+0x3d6)
//   p4  = rolling seq counter hi           p5 = seq lo (param_1+0x4e8, ++ each)
//   p6  = date day     p7 = date month     p8 = (year & 0xff) + '0'
//   p9..p12 = feature-circuit states (FUN_006eaa40 loop, 4 bytes)
//   p13 = circuit state (FUN_00969be4 idx 0)   p14 = idx 7
//   p15 = OR of circuit states idx 1,2,3 (FUN_00969be4)
//   p16..p18 = body/heat-source bytes; p19 = body-active bitmap from list tok 0x8b54
//   p20/p21 = water temps (FUN_00831698 idx 0/1)
//   p22 = heat-mode/equipment flags (sys-mode-dependent bitfield)
//   p24 = freeze/delay state (FUN_0068a1c0 -> 0 or 4)
//   p27..p29 = memset 0 then circuit-on bits (token 0xc140 body 0/1)
//   p31..p34 = comm-lost bitmask (token 0x9172, several device categories)
//   p37..p38 = schedule/egg-timer countdown (secs -> hh:mm, 0xff/0 when none)
//   p40 = token via FUN_007d92cc            p41 = 0x50/0x51 (param_1+0x4ea)
//   p42 = panel-discovery handshake byte (0xfb/0xfd/0xfe/0xff state machine)
//   p46 = clock hour   p47 = clock minute   (p48/second etc.)  + circuit bits
//
// WHY NOT EMITTED (decision): both frames are OCP-owned aggregates
// that read the full datamodel. To emit CORRECT bytes (the project's
// no-guesses rule) the ESP would have to own an equivalent for every field
// (multi-index water temps, heat-source nibble config, valve/freeze/schedule
// state, comm-lost bitmask, panel-discovery handshake), and it does not. Stuffing
// the un-ownable ~70% of each frame with placeholder zeros would broadcast
// WRONG state to any listening Pentair remote/panel, which is worse than
// silence. In the target deployment the ESP is the sole master with HA/ESPHome
// as the UI and NO Pentair remotes/panels on the bus, so there is no consumer
// for this broadcast. Revisit only if the ESP grows first-class ownership of
// the datamodel fields above.

// ---- IntelliChem (A5 bus) ---------------------------------------------
// [E] 30s poll: cmd 0xd2 with a single payload byte 0xD2 (binder FUN_0096f3b0)
// -> reply 0x12. The 0x12 reply is parsed by IntelliChem_comm's status handler
// FUN_0096fed0, whose fields the firmware names in its own debug string
// "[intellichem] Reading: ph set[] orp set[] ph tank[] orp tank[] hardness[]
// acid[] alkalinity[] saturation[]". The wire layout below is therefore
// firmware-proven [E] (data index = reply payload byte, base = message +0x8b).
//
// The 0x92 config-write path is now firmware-proven [E] too: binder
// FUN_0096f3bc reads seven data-model tokens and packs a 21-byte payload
// (registerMessageTemplate cmd 0x92, len 0x15) that the IntelliChem answers
// with an ACK (response cmd 1). Each field is big-endian, matching the 0x12
// read side (see ChemConfig / build_chem_set_payload below).
static const uint8_t CHEM_CMD_STATUS = 0xd2;      // data: [0xD2]; reply 0x12
static const uint8_t CHEM_CMD_STATUS_REPLY = 0x12;
static const uint8_t CHEM_CMD_SET = 0x92;         // config-write; reply = ACK (cmd 1)
static const uint8_t CHEM_CMD_ACK = 0x01;         // ACK reply to a 0x92 write
static const uint8_t CHEM_SET_PAYLOAD_LEN = 21;   // firmware template length 0x15
static const uint32_t CHEM_POLL_INTERVAL_MS = 30000;

// Minimum 0x12 reply payload length to decode every field below (index 28).
static const uint8_t CHEM_STATUS_MIN_LEN = 29;

// Minimum length to also decode the alarm/warning/dosing bytes (index 35).
// The alarm block sits past the chemistry readings; when a captured reply is
// shorter (index < 35) the alarm fields stay zero (has_alarms=false).
static const uint8_t CHEM_STATUS_ALARM_MIN_LEN = 36;

// IntelliChem alarm byte (data[32]) bit -> label. Bits 0-6 are bit-tested by
// the firmware status/alert namer (FUN_008c1dfc); bit 7 (Probe Fault) is taken
// from the tagyoureit/nodejs-poolController byte map (byte 32) which agrees
// with the firmware field position [E].
static const uint8_t CHEM_ALARM_NO_FLOW = 0x01;         // bit 0 -> "No Flow"
static const uint8_t CHEM_ALARM_PH_HIGH = 0x02;         // bit 1 -> "pH High"
static const uint8_t CHEM_ALARM_PH_LOW = 0x04;          // bit 2 -> "pH Low"
static const uint8_t CHEM_ALARM_ORP_HIGH = 0x08;        // bit 3 -> "ORP High"
static const uint8_t CHEM_ALARM_ORP_LOW = 0x10;         // bit 4 -> "ORP Low"
static const uint8_t CHEM_ALARM_CHECK_PH_TANK = 0x20;   // bit 5 -> "Check pH Container"
static const uint8_t CHEM_ALARM_CHECK_ORP_TANK = 0x40;  // bit 6 -> "Check ORP Container"
static const uint8_t CHEM_ALARM_PROBE_FAULT = 0x80;     // bit 7 -> "Probe Fault"

// IntelliChem warning byte (data[33]) bit -> label (firmware namer bit tests).
static const uint8_t CHEM_WARN_PH_LOCKOUT = 0x01;      // bit 0 -> "pH Lockout"
static const uint8_t CHEM_WARN_PH_FEED_LIMIT = 0x02;   // bit 1 -> "pH Daily Feed Limit"
static const uint8_t CHEM_WARN_ORP_FEED_LIMIT = 0x04;  // bit 2 -> "ORP Daily Feed Limit"
static const uint8_t CHEM_WARN_INVALID_SETUP = 0x08;   // bit 3 -> "Invalid Setup"
static const uint8_t CHEM_WARN_COMM_ERROR = 0x10;      // bit 4 -> "Peripheral Comm Error"
static const uint8_t CHEM_WARN_AUTO_CAL_FAILED = 0x40;  // bit 6 -> "Auto Calibration Failed"

// pH/ORP doser run state, from data[34] high nibble (firmware namer): the pH
// doser uses bits 4-5, the ORP doser bits 6-7. njsPC's worked example
// (byte 34 = 0x95 -> pH Monitoring, ORP Mixing) matches this decode [E].
enum ChemDosingStatus: uint8_t {
  CHEM_DOSING_DOSING = 0,
  CHEM_DOSING_MONITORING = 1,
  CHEM_DOSING_MIXING = 2,
};

// Decoded IntelliChem status telemetry (real units).
struct ChemStatus {
  float ph{0};                  // data[0..1] BE16 / 100
  uint16_t orp{0};              // data[2..3] BE16, mV
  float ph_setpoint{0};         // data[4..5] BE16 / 100
  uint16_t orp_setpoint{0};     // data[6..7] BE16, mV
  uint8_t ph_tank{0};           // data[20], level 1-6
  uint8_t orp_tank{0};          // data[21], level 1-6
  float saturation_index{0};    // data[22] int8 / 100 (Langelier, signed)
  uint16_t calcium_hardness{0}; // data[23..24] BE16, ppm
  uint16_t cyanuric_acid{0};    // data[25..26] BE16, ppm (firmware "acid")
  uint16_t total_alkalinity{0}; // data[27..28] BE16, ppm
  bool has_alarms{false};       // true when data[32..35] were present/decoded
  uint8_t alarms{0};            // data[32] bitfield (CHEM_ALARM_*)
  uint8_t warnings{0};          // data[33] bitfield (CHEM_WARN_*)
  uint8_t ph_dosing{0};         // data[34] bits 4-5 (ChemDosingStatus)
  uint8_t orp_dosing{0};        // data[34] bits 6-7 (ChemDosingStatus)
  bool flow_delay{false};       // data[35] bit 1 -> "Flow Delay Active"
};

// Pure decoder (host-testable). Returns false if the payload is too short.
inline bool decode_chem_status(const uint8_t *d, size_t len, ChemStatus &out) {
  if (d == nullptr || len < CHEM_STATUS_MIN_LEN)
    return false;
  auto be16 = [](uint8_t hi, uint8_t lo) -> uint16_t {
    return static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
  };
  out.ph = be16(d[0], d[1]) / 100.0f;
  out.orp = be16(d[2], d[3]);
  out.ph_setpoint = be16(d[4], d[5]) / 100.0f;
  out.orp_setpoint = be16(d[6], d[7]);
  out.ph_tank = d[20];
  out.orp_tank = d[21];
  out.saturation_index = static_cast<int8_t>(d[22]) / 100.0f;
  out.calcium_hardness = be16(d[23], d[24]);
  out.cyanuric_acid = be16(d[25], d[26]);
  out.total_alkalinity = be16(d[27], d[28]);
  if (len >= CHEM_STATUS_ALARM_MIN_LEN) {
    out.has_alarms = true;
    out.alarms = d[32];
    out.warnings = d[33];
    out.ph_dosing = (d[34] >> 4) & 0x03;
    out.orp_dosing = (d[34] >> 6) & 0x03;
    out.flow_delay = (d[35] & 0x02) != 0;
  }
  return true;
}

// The seven settable IntelliChem config fields, in wire order. Firmware binder
// FUN_0096f3bc reads these from data-model tokens (cf4c/c4e0/d034/c5c8/8cb0/
// 954c/83b8) and writes them big-endian into the 0x92 payload. The pH/ORP set-
// points and the LSI inputs (hardness, CYA, alkalinity) are all sent together
// in a single message, so a caller that changes one field must resend the
// others unchanged (seed this from the last decoded ChemStatus).
struct ChemConfig {
  uint16_t ph_setpoint_x100{0};  // token cf4c; pH setpoint * 100 (720 = 7.20)
  uint16_t orp_setpoint{0};      // token c4e0; ORP setpoint, mV
  uint8_t ph_tank{0};            // token d034; acid/pH tank level 1-6
  uint8_t orp_tank{0};           // token c5c8; ORP tank level 1-6
  uint16_t calcium_hardness{0};  // token 8cb0; ppm
  uint16_t cyanuric_acid{0};     // token 954c; ppm
  uint16_t total_alkalinity{0};  // token 83b8; ppm
};

// Pack the 21-byte 0x92 config-write payload, mirroring firmware binder
// FUN_0096f3bc byte-for-byte: 12 meaningful big-endian bytes followed by nine
// zero pad bytes. out must point at CHEM_SET_PAYLOAD_LEN (21) bytes.
inline void build_chem_set_payload(const ChemConfig &cfg, uint8_t *out) {
  out[0] = static_cast<uint8_t>(cfg.ph_setpoint_x100 >> 8);
  out[1] = static_cast<uint8_t>(cfg.ph_setpoint_x100 & 0xFF);
  out[2] = static_cast<uint8_t>(cfg.orp_setpoint >> 8);
  out[3] = static_cast<uint8_t>(cfg.orp_setpoint & 0xFF);
  out[4] = cfg.ph_tank;
  out[5] = cfg.orp_tank;
  out[6] = static_cast<uint8_t>(cfg.calcium_hardness >> 8);
  out[7] = static_cast<uint8_t>(cfg.calcium_hardness & 0xFF);
  out[8] = static_cast<uint8_t>(cfg.cyanuric_acid >> 8);
  out[9] = static_cast<uint8_t>(cfg.cyanuric_acid & 0xFF);
  out[10] = static_cast<uint8_t>(cfg.total_alkalinity >> 8);
  out[11] = static_cast<uint8_t>(cfg.total_alkalinity & 0xFF);
  for (uint8_t i = 12; i < CHEM_SET_PAYLOAD_LEN; i++)
    out[i] = 0;
}

// Human-readable name for a doser run state (data[34] nibble).
inline const char *chem_dosing_status_name(uint8_t v) {
  switch (v) {
    case CHEM_DOSING_DOSING:
      return "Dosing";
    case CHEM_DOSING_MONITORING:
      return "Monitoring";
    case CHEM_DOSING_MIXING:
      return "Mixing";
    default:
      return "Unknown";
  }
}

// Comma-separated list of active IntelliChem alarms (data[32]); "" if none.
inline std::string chem_alarm_string(uint8_t a) {
  std::string s;
  auto add = [&](const char *n) {
    if (!s.empty())
      s += ", ";
    s += n;
  };
  if (a & CHEM_ALARM_NO_FLOW)
    add("No Flow");
  if (a & CHEM_ALARM_PH_HIGH)
    add("pH High");
  if (a & CHEM_ALARM_PH_LOW)
    add("pH Low");
  if (a & CHEM_ALARM_ORP_HIGH)
    add("ORP High");
  if (a & CHEM_ALARM_ORP_LOW)
    add("ORP Low");
  if (a & CHEM_ALARM_CHECK_PH_TANK)
    add("Check pH Container");
  if (a & CHEM_ALARM_CHECK_ORP_TANK)
    add("Check ORP Container");
  if (a & CHEM_ALARM_PROBE_FAULT)
    add("Probe Fault");
  return s;
}

// Comma-separated list of active IntelliChem warnings (data[33]); "" if none.
inline std::string chem_warning_string(uint8_t w) {
  std::string s;
  auto add = [&](const char *n) {
    if (!s.empty())
      s += ", ";
    s += n;
  };
  if (w & CHEM_WARN_PH_LOCKOUT)
    add("pH Lockout");
  if (w & CHEM_WARN_PH_FEED_LIMIT)
    add("pH Daily Feed Limit");
  if (w & CHEM_WARN_ORP_FEED_LIMIT)
    add("ORP Daily Feed Limit");
  if (w & CHEM_WARN_INVALID_SETUP)
    add("Invalid Setup");
  if (w & CHEM_WARN_COMM_ERROR)
    add("Peripheral Comm Error");
  if (w & CHEM_WARN_AUTO_CAL_FAILED)
    add("Auto Calibration Failed");
  return s;
}


// Pump type ids match the firmware enum (token 0xef04).
enum PumpType: uint8_t {
  PUMP_TYPE_VS = 3,
  PUMP_TYPE_VSF = 4,
  PUMP_TYPE_VF = 5,
};

// VSF setpoint mode: whether the setpoint value is RPM or GPM.
enum PumpMode: uint8_t {
  PUMP_MODE_SPEED = 1,  // RPM
  PUMP_MODE_FLOW = 2,   // GPM
};

// Firmware-derived setpoint clamps.
static const uint16_t RPM_MIN = 450, RPM_MAX = 3450;
static const uint16_t GPM_MIN = 20, GPM_MAX = 140;

// ---- setpoint encoding (firmware-derived) ------------------------------
// These are pure, framework-independent functions so the on-wire encoding can
// be unit-tested off-device against the firmware-derived spec (see test/).

// Clamp a target to the pump's native unit range (RPM for VS / VSF-speed,
// GPM for VF / VSF-flow).
inline uint16_t clamp_setpoint(PumpType type, PumpMode mode, uint16_t target) {
  bool is_flow = (type == PUMP_TYPE_VF) || (type == PUMP_TYPE_VSF && mode == PUMP_MODE_FLOW);
  uint16_t lo = is_flow ? GPM_MIN: RPM_MIN;
  uint16_t hi = is_flow ? GPM_MAX: RPM_MAX;
  if (target < lo)
    target = lo;
  if (target > hi)
    target = hi;
  return target;
}

// Encode the setpoint command + payload exactly as the firmware does: the
// per-generation register selector prefix followed by the clamped value
// (BE16 for VS/VSF, low byte for VF). VS/VF use cmd 1, VSF uses cmd 9.
inline void encode_setpoint(PumpType type, PumpMode mode, uint16_t target, uint8_t &cmd,
                            std::vector<uint8_t> &data) {
  uint16_t v = clamp_setpoint(type, mode, target);
  data.clear();
  switch (type) {
    case PUMP_TYPE_VS:  // cmd 1, register 02 C4, BE16 RPM
      cmd = PUMP_CMD_SETPOINT;
      data = {REG_VS_HI, REG_VS_LO, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)};
      break;
    case PUMP_TYPE_VF:  // cmd 1, register 02 E4, GPM in low byte
      cmd = PUMP_CMD_SETPOINT;
      data = {REG_VF_HI, REG_VF_LO, 0x00, static_cast<uint8_t>(v & 0xFF)};
      break;
    case PUMP_TYPE_VSF:  // register 03 27, BE16; cmd 0x0A Speed(RPM) / 0x09 Flow(GPM) by mode
    default:
      cmd = (mode == PUMP_MODE_FLOW) ? PUMP_CMD_SETPOINT_VSF: PUMP_CMD_SETPOINT_VSF_SPEED;
      data = {REG_VSF_HI, REG_VSF_LO, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)};
      break;
  }
}

// ---- checksum helpers ------------------------------------------------------

// 16-bit additive checksum over [begin, end).
inline uint16_t a5_checksum(const uint8_t *begin, const uint8_t *end) {
  uint16_t sum = 0;
  for (const uint8_t *p = begin; p != end; ++p)
    sum += *p;
  return sum;
}

// Low byte of the additive sum over [begin, end).
inline uint8_t chlor_checksum(const uint8_t *begin, const uint8_t *end) {
  uint16_t sum = 0;
  for (const uint8_t *p = begin; p != end; ++p)
    sum += *p;
  return static_cast<uint8_t>(sum & 0xFF);
}

// Build a full A5 frame (including preamble and checksum) into out.
inline void build_a5_frame(std::vector<uint8_t> &out, uint8_t dst, uint8_t src, uint8_t cmd,
                           const uint8_t *data, uint8_t len) {
  out.clear();
  out.push_back(A5_PREAMBLE_0);
  out.push_back(A5_PREAMBLE_1);
  out.push_back(A5_PREAMBLE_2);
  size_t sum_start = out.size();  // checksum starts at the A5 byte
  out.push_back(A5_START);
  out.push_back(A5_VERSION);
  out.push_back(dst);
  out.push_back(src);
  out.push_back(cmd);
  out.push_back(len);
  for (uint8_t i = 0; i < len; i++)
    out.push_back(data[i]);
  uint16_t ck = a5_checksum(&out[sum_start], &out[out.size()]);
  out.push_back(static_cast<uint8_t>(ck >> 8));
  out.push_back(static_cast<uint8_t>(ck & 0xFF));
}

// Build a full IntelliChlor frame (including DLE framing and checksum) into out.
inline void build_chlor_frame(std::vector<uint8_t> &out, uint8_t dst, uint8_t cmd,
                              const uint8_t *data, uint8_t len) {
  out.clear();
  out.push_back(CHLOR_DLE);
  out.push_back(CHLOR_STX);
  out.push_back(dst);
  out.push_back(cmd);
  for (uint8_t i = 0; i < len; i++)
    out.push_back(data[i]);
  // Checksum spans the whole message including the 10 02 header, up to the last
  // data byte (this is what real IntelliChlor cells validate against).
  uint8_t ck = chlor_checksum(&out[0], &out[out.size()]);
  out.push_back(ck);
  out.push_back(CHLOR_DLE);
  out.push_back(CHLOR_ETX);
}

// Parsed inbound frame handed to device handlers.
struct RxFrame {
  bool is_chlor;    // true = IntelliChlor (10/02) frame, false = A5 frame
  uint8_t dst;
  uint8_t src;      // A5 only
  uint8_t cmd;
  std::vector<uint8_t> data;
};

}  // namespace pentair
}  // namespace esphome
