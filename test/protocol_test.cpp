// Golden-vector tests for the Pentair wire protocol.
//
// These validate the framework-independent encoders in components/pentair/
// protocol.h against the firmware-derived spec summarised in README.md
// ("Firmware provenance & protocol overview").
// Every expected value below is annotated with the doc section it comes from,
// so a regression here means the library has drifted from "how the real
// firmware works" as reverse-engineered from ICmain_3_008.elf.
//
// Pure host build, no ESPHome / no toolchain download:
//   c++ -std=c++17 -I../components/pentair protocol_test.cpp -o /tmp/ptest && /tmp/ptest
// (or just run./run.sh)

#include "protocol.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace esphome::pentair;

static int g_failures = 0;
static int g_checks = 0;

static std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char buf[4];
  for (size_t i = 0; i < v.size(); i++) {
    std::snprintf(buf, sizeof(buf), "%02X", v[i]);
    if (i)
      s += ' ';
    s += buf;
  }
  return s;
}

static void check_bytes(const char *what, const std::vector<uint8_t> &got,
                        const std::vector<uint8_t> &want) {
  g_checks++;
  if (got != want) {
    g_failures++;
    std::printf("  FAIL %s\n        got:  %s\n        want: %s\n", what, hex(got).c_str(),
                hex(want).c_str());
  } else {
    std::printf("  ok   %s  [%s]\n", what, hex(got).c_str());
  }
}

static void check_close(const char *what, double got, double want) {
  g_checks++;
  double diff = got - want;
  if (diff < 0)
    diff = -diff;
  if (diff > 1e-4) {
    g_failures++;
    std::printf("  FAIL %s  got=%f want=%f\n", what, got, want);
  } else {
    std::printf("  ok   %s  = %f\n", what, got);
  }
}

static void check_eq(const char *what, long got, long want) {
  g_checks++;
  if (got != want) {
    g_failures++;
    std::printf("  FAIL %s  got=%ld want=%ld\n", what, got, want);
  } else {
    std::printf("  ok   %s  = %ld\n", what, got);
  }
}

static void check_str(const char *what, const std::string &got, const std::string &want) {
  g_checks++;
  if (got != want) {
    g_failures++;
    std::printf("  FAIL %s  got=\"%s\" want=\"%s\"\n", what, got.c_str(), want.c_str());
  } else {
    std::printf("  ok   %s  = \"%s\"\n", what, got.c_str());
  }
}

// ---------------------------------------------------------------------------

// constants that pin firmware-established facts. If any of these
// drift, generated frames stop matching real devices.
static void test_constants() {
  std::printf("[constants] frame markers, command codes, enums\n");
  // A5 physical framing markers.
  check_eq("A5 preamble FF 00 FF", (A5_PREAMBLE_0 << 16) | (A5_PREAMBLE_1 << 8) | A5_PREAMBLE_2,
           0xFF00FF);
  check_eq("A5 start byte", A5_START, 0xA5);
  // Chlorinator DLE framing.
  check_eq("CHLOR 10 02 header", (CHLOR_DLE << 8) | CHLOR_STX, 0x1002);
  check_eq("CHLOR 10 03 trailer", (CHLOR_DLE << 8) | CHLOR_ETX, 0x1003);
  // Pump command codes.
  check_eq("cmd VS/VF setpoint = 1", PUMP_CMD_SETPOINT, 0x01);
  check_eq("cmd remote (HandOn) = 4", PUMP_CMD_REMOTE, 0x04);
  check_eq("cmd run = 6", PUMP_CMD_RUN, 0x06);
  check_eq("cmd status = 7", PUMP_CMD_STATUS, 0x07);
  check_eq("cmd VSF flow (GPM) = 9", PUMP_CMD_SETPOINT_VSF, 0x09);
  check_eq("cmd VSF speed (RPM) = 0x0A", PUMP_CMD_SETPOINT_VSF_SPEED, 0x0A);
  check_eq("cmd VF feature = 5", PUMP_CMD_FEATURE, 0x05);
  check_eq("VF feature payload = 6", PUMP_FEATURE_1, 0x06);
  // HandOn / run payload bytes.
  check_eq("HandOn payload 0xFF", PUMP_REMOTE_ON, 0xFF);
  check_eq("HandOff payload 0x00", PUMP_REMOTE_OFF, 0x00);
  check_eq("run byte 0x0A", PUMP_RUN, 0x0A);
  check_eq("stop byte 0x04", PUMP_STOP, 0x04);
  // Setpoint register selector prefixes.
  check_eq("VS  register 02 C4", (REG_VS_HI << 8) | REG_VS_LO, 0x02C4);
  check_eq("VF  register 02 E4", (REG_VF_HI << 8) | REG_VF_LO, 0x02E4);
  check_eq("VSF register 03 27", (REG_VSF_HI << 8) | REG_VSF_LO, 0x0327);
  // Chlorinator command codes.
  check_eq("chlor set output = 0x11", CHLOR_CMD_SET_OUTPUT, 0x11);
  check_eq("chlor status = 0x12", CHLOR_CMD_STATUS, 0x12);
  // Pump-type enum from model token 0xef04: 3=VS, 4=VSF, 5=VF.
  check_eq("PUMP_TYPE_VS = 3", PUMP_TYPE_VS, 3);
  check_eq("PUMP_TYPE_VSF = 4", PUMP_TYPE_VSF, 4);
  check_eq("PUMP_TYPE_VF = 5", PUMP_TYPE_VF, 5);
  // Firmware setpoint clamp ranges.
  check_eq("RPM range 450..3450", (RPM_MIN << 16) | RPM_MAX, (450 << 16) | 3450);
  check_eq("GPM range 20..140", (GPM_MIN << 16) | GPM_MAX, (20 << 16) | 140);
}

// A5 additive checksum spans A5..last-data byte; 16-bit, hi then lo.
static void test_a5_framing() {
  std::printf("[a5] physical frame layout + 16-bit additive checksum\n");

  // Pump status poll: dst=0x60, src=0x10, cmd=0x07, no payload.
  // sum(A5 00 60 10 07 00) = 0x011C -> ckHi=01 ckLo=1C.
  std::vector<uint8_t> f;
  build_a5_frame(f, 0x60, 0x10, PUMP_CMD_STATUS, nullptr, 0);
  check_bytes("status poll @0x60",
              f, {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x60, 0x10, 0x07, 0x00, 0x01, 0x1C});

  // HandOn (remote-control hold): cmd=0x04, payload 0xFF.
  // sum(A5 00 60 10 04 01 FF) = 0x0219 -> 02 19.
  uint8_t on = PUMP_REMOTE_ON;
  build_a5_frame(f, 0x60, 0x10, PUMP_CMD_REMOTE, &on, 1);
  check_bytes("HandOn 0xFF @0x60",
              f, {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x60, 0x10, 0x04, 0x01, 0xFF, 0x02, 0x19});

  // Checksum must cover A5 through last data byte, big-endian appended.
  uint16_t want = 0;
  for (size_t i = 3; i + 2 < f.size(); i++)  // from A5 (index 3) to last data byte
    want += f[i];
  check_eq("HandOn checksum hi", f[f.size() - 2], (want >> 8) & 0xFF);
  check_eq("HandOn checksum lo", f[f.size() - 1], want & 0xFF);
}

// IntelliChlor frame 10 02 dst cmd data ck 10 03; checksum includes 10 02.
static void test_chlor_framing() {
  std::printf("[chlor] 10/02 frame layout + checksum incl. header\n");

  // Set output 50% (0x32) to dst 0x00.
  // sum(10 02 00 11 32) = 0x55.
  uint8_t out = 50;
  std::vector<uint8_t> f;
  build_chlor_frame(f, 0x00, CHLOR_CMD_SET_OUTPUT, &out, 1);
  check_bytes("set output 50%", f, {0x10, 0x02, 0x00, 0x11, 0x32, 0x55, 0x10, 0x03});

  // Checksum spans 10 02.. last data byte (low byte of sum).
  uint16_t want = 0;
  for (size_t i = 0; i + 3 < f.size(); i++)  // through the byte before <ck> 10 03
    want += f[i];
  check_eq("chlor checksum", f[f.size() - 3], want & 0xFF);
}

// + firmware-authoritative labels (consumer FUN_006f5b94 bit test, label
// switch in alert-widget paintEvent FUN_0081a8e8). The panel tests exactly bits
// 0,1,2,4,6 -> "No Flow" / "Low Salt" / "Very Low Salt" / "Inspect Cell" /
// "Cold Water Cutoff". Real capture 10 02 00 12 3A 90 <ck> 10 03 -> salt
// 0x3A*50 = 2900 ppm; alarms 0x90 = bit4 "Inspect Cell" (+ bit7, undecoded).
static void test_chlor_status_decode() {
  std::printf("[chlor] cmd 0x12 status decode: salt*50 + alarm bits (firmware labels)\n");
  const uint8_t salt_raw = 0x3A;  // 58
  check_eq("salt 0x3A -> 2900 ppm", int(salt_raw * CHLOR_SALT_SCALE), 2900);

  // Bit -> mask, verbatim from the firmware decode.
  check_eq("no-flow bit", CHLOR_ALARM_NO_FLOW, 0x01);
  check_eq("low-salt bit", CHLOR_ALARM_LOW_SALT, 0x02);
  check_eq("very-low-salt bit", CHLOR_ALARM_VERY_LOW_SALT, 0x04);
  check_eq("clean/inspect-cell bit", CHLOR_ALARM_CLEAN_CELL, 0x10);
  check_eq("cold-water bit", CHLOR_ALARM_COLD_WATER, 0x40);

  // Captured 0x90 = bit4 (Inspect Cell) + bit7 (undecoded). Panel shows
  // "Inspect Cell"; it is NOT no-flow / low-salt / very-low-salt / cold-water.
  const uint8_t alarms = 0x90;
  check_eq("0x90 -> clean cell", (alarms & CHLOR_ALARM_CLEAN_CELL) != 0, true);
  check_eq("0x90 -> not no-flow", (alarms & CHLOR_ALARM_NO_FLOW) != 0, false);
  check_eq("0x90 -> not low-salt", (alarms & CHLOR_ALARM_LOW_SALT) != 0, false);
  check_eq("0x90 -> not very-low-salt", (alarms & CHLOR_ALARM_VERY_LOW_SALT) != 0, false);
  check_eq("0x90 -> not cold-water", (alarms & CHLOR_ALARM_COLD_WATER) != 0, false);

  // A no-flow alarm (0x01) must NOT read as low-salt (guards a bit-shift bug).
  check_eq("0x01 -> no-flow", (0x01 & CHLOR_ALARM_NO_FLOW) != 0, true);
  check_eq("0x01 -> not low-salt", (0x01 & CHLOR_ALARM_LOW_SALT) != 0, false);
}

// per-generation setpoint encoders, clamps, and value endianness.
static void test_setpoint_encoding() {
  std::printf("[setpoint] per-type register + BE16/lowbyte value + clamp\n");
  uint8_t cmd = 0;
  std::vector<uint8_t> d;

  // VS: cmd 1, 02 C4, BE16 RPM. 3000 = 0x0BB8.
  encode_setpoint(PUMP_TYPE_VS, PUMP_MODE_SPEED, 3000, cmd, d);
  check_eq("VS cmd = 1", cmd, PUMP_CMD_SETPOINT);
  check_bytes("VS 3000 rpm payload", d, {0x02, 0xC4, 0x0B, 0xB8});

  // VSF speed: cmd 0x0A (Speed), 03 27, BE16 RPM.
  encode_setpoint(PUMP_TYPE_VSF, PUMP_MODE_SPEED, 3000, cmd, d);
  check_eq("VSF speed cmd = 0x0A", cmd, PUMP_CMD_SETPOINT_VSF_SPEED);
  check_bytes("VSF 3000 rpm payload", d, {0x03, 0x27, 0x0B, 0xB8});

  // VSF flow: cmd 0x09 (Flow), 03 27, BE16 GPM. 80 = 0x0050.
  encode_setpoint(PUMP_TYPE_VSF, PUMP_MODE_FLOW, 80, cmd, d);
  check_eq("VSF flow cmd = 0x09", cmd, PUMP_CMD_SETPOINT_VSF);
  check_bytes("VSF 80 gpm payload", d, {0x03, 0x27, 0x00, 0x50});

  // VF: cmd 1, 02 E4, GPM in low byte, high byte 0x00.
  encode_setpoint(PUMP_TYPE_VF, PUMP_MODE_FLOW, 80, cmd, d);
  check_eq("VF cmd = 1", cmd, PUMP_CMD_SETPOINT);
  check_bytes("VF 80 gpm payload", d, {0x02, 0xE4, 0x00, 0x50});

  // Clamp: RPM below 450 -> 450 (0x01C2); above 3450 -> 3450 (0x0D7A).
  encode_setpoint(PUMP_TYPE_VS, PUMP_MODE_SPEED, 100, cmd, d);
  check_bytes("VS clamp low -> 450", d, {0x02, 0xC4, 0x01, 0xC2});
  encode_setpoint(PUMP_TYPE_VS, PUMP_MODE_SPEED, 9999, cmd, d);
  check_bytes("VS clamp high -> 3450", d, {0x02, 0xC4, 0x0D, 0x7A});

  // Clamp: GPM below 20 -> 20; above 140 -> 140.
  encode_setpoint(PUMP_TYPE_VF, PUMP_MODE_FLOW, 1, cmd, d);
  check_bytes("VF clamp low -> 20", d, {0x02, 0xE4, 0x00, 0x14});
  encode_setpoint(PUMP_TYPE_VF, PUMP_MODE_FLOW, 999, cmd, d);
  check_bytes("VF clamp high -> 140", d, {0x02, 0xE4, 0x00, 0x8C});

  // VF feature/menu select frame (cmd 5, payload 0x06) the OCP sends to VF
  // pumps in the drive batch (firmware VF setupMessages + binder FUN_0096baf0).
  // @0x60: sum(A5 00 60 10 05 01 06) = 0x0121 -> 01 21.
  std::vector<uint8_t> ff;
  uint8_t feat = PUMP_FEATURE_1;
  build_a5_frame(ff, 0x60, 0x10, PUMP_CMD_FEATURE, &feat, 1);
  check_bytes("VF feature frame @0x60", ff,
              {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x60, 0x10, 0x05, 0x01, 0x06, 0x01, 0x21});
}

// heater per-subtype request command, length, poll cadence, and reply
// routing (firmware factory FUN_009765d8 / setup 009778dc).
static void test_heater() {
  std::printf("[heater] per-subtype request/reply codes + cadence\n");
  // Request command by subtype.
  check_eq("Ultra req = 0x72", heater_request_cmd(HEATER_TYPE_ULTRA), 0x72);
  check_eq("Hybrid req = 0x70", heater_request_cmd(HEATER_TYPE_HYBRID), 0x70);
  check_eq("MasterTemp req = 0x70", heater_request_cmd(HEATER_TYPE_MASTERTEMP), 0x70);
  check_eq("MaxE-Therm req = 0x70", heater_request_cmd(HEATER_TYPE_MAXETHERM), 0x70);
  check_eq("ETI250 req = 0x80", heater_request_cmd(HEATER_TYPE_ETI250), 0x80);
  // Declared request payload length.
  check_eq("MasterTemp req len = 10", heater_request_len(HEATER_TYPE_MASTERTEMP), 10);
  check_eq("ETI250 req len = 12", heater_request_len(HEATER_TYPE_ETI250), 12);
  // Poll cadence: Ultra 30 s, all others 50 s.
  check_eq("Ultra poll = 30000 ms", heater_poll_interval_ms(HEATER_TYPE_ULTRA), 30000);
  check_eq("Hybrid poll = 50000 ms", heater_poll_interval_ms(HEATER_TYPE_HYBRID), 50000);
  // Reply command routing (0x71/0x73/0x74/0x81 are heater replies).
  check_eq("0x73 is heater reply", is_heater_reply_cmd(0x73), 1);
  check_eq("0x74 is heater reply", is_heater_reply_cmd(0x74), 1);
  check_eq("0x81 is heater reply", is_heater_reply_cmd(0x81), 1);
  check_eq("0x07 (pump) not heater reply", is_heater_reply_cmd(0x07), 0);

  // Status-request frame is a well-formed A5 frame at the declared length
  // (payload zero-filled: content is not RE-decoded). MasterTemp @0x70:
  // sum(A5 00 70 10 70 0A + ten 00) = 0x019F -> 01 9F.
  std::vector<uint8_t> data(heater_request_len(HEATER_TYPE_MASTERTEMP), 0);
  std::vector<uint8_t> f;
  build_a5_frame(f, 0x70, 0x10, heater_request_cmd(HEATER_TYPE_MASTERTEMP), data.data(),
                 data.size());
  check_bytes("MasterTemp status req @0x70",
              f, {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x70, 0x10, 0x70, 0x0A, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x9F});

  // Status-reply field offsets within the A5 data payload.
  check_eq("heaterMode offset = 1", HEATER_OFF_MODE, 1);
  check_eq("HeaterStatus offset = 2", HEATER_OFF_STATUS, 2);
  check_eq("ErrorFlagsA offset = 3", HEATER_OFF_ERR_A, 3);
  check_eq("ErrorFlagsB offset = 4", HEATER_OFF_ERR_B, 4);
  check_eq("FenwalDiag offset = 13", HEATER_OFF_FENWAL, 13);
}

// IntelliChem 30 s poll cmd 0xd2 (single payload byte 0xD2) -> reply 0x12
// (binder FUN_0096f3b0).
static void test_intellichem() {
  std::printf("[intellichem] 0xd2 poll payload + reply code + cadence\n");
  check_eq("poll cmd = 0xd2", CHEM_CMD_STATUS, 0xd2);
  check_eq("reply cmd = 0x12", CHEM_CMD_STATUS_REPLY, 0x12);
  check_eq("poll interval = 30000 ms", CHEM_POLL_INTERVAL_MS, 30000);

  // Poll frame to IntelliChem @0x90: cmd 0xd2, one payload byte 0xD2.
  // sum(A5 00 90 10 D2 01 D2) = 0x02EA -> 02 EA.
  uint8_t data = CHEM_CMD_STATUS;
  std::vector<uint8_t> f;
  build_a5_frame(f, 0x90, 0x10, CHEM_CMD_STATUS, &data, 1);
  check_bytes("IntelliChem poll @0x90",
              f, {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x90, 0x10, 0xD2, 0x01, 0xD2, 0x02, 0xEA});
}

// IntelliChem 0x12 status field decode. Wire layout proven from firmware
// parser FUN_0096fed0 (its own debug string names the fields). data index =
// reply payload byte (base = message +0x8b).
static void test_intellichem_status_decode() {
  std::printf("[intellichem] 0x12 status field decode (firmware FUN_0096fed0)\n");
  std::vector<uint8_t> d(CHEM_STATUS_MIN_LEN, 0);
  d[0] = 0x02; d[1] = 0xF0;   // pH 752 -> 7.52
  d[2] = 0x02; d[3] = 0x8A;   // ORP 650 mV
  d[4] = 0x02; d[5] = 0xF8;   // pH set 760 -> 7.60
  d[6] = 0x02; d[7] = 0xBC;   // ORP set 700 mV
  d[20] = 5;                  // pH tank level
  d[21] = 6;                  // ORP tank level
  d[22] = 0xEB;               // saturation index int8 -21 -> -0.21
  d[23] = 0x01; d[24] = 0x90; // calcium hardness 400 ppm
  d[25] = 0x00; d[26] = 0x32; // cyanuric acid 50 ppm
  d[27] = 0x00; d[28] = 0x64; // total alkalinity 100 ppm

  ChemStatus st;
  check_eq("decode ok (len 29)", decode_chem_status(d.data(), d.size(), st), 1);
  check_close("pH = 7.52", st.ph, 7.52);
  check_eq("ORP = 650 mV", st.orp, 650);
  check_close("pH setpoint = 7.60", st.ph_setpoint, 7.60);
  check_eq("ORP setpoint = 700 mV", st.orp_setpoint, 700);
  check_eq("pH tank = 5", st.ph_tank, 5);
  check_eq("ORP tank = 6", st.orp_tank, 6);
  check_close("saturation index = -0.21", st.saturation_index, -0.21);
  check_eq("calcium hardness = 400", st.calcium_hardness, 400);
  check_eq("cyanuric acid = 50", st.cyanuric_acid, 50);
  check_eq("total alkalinity = 100", st.total_alkalinity, 100);

  // Short payload must be rejected (no partial/garbage publish).
  ChemStatus st2;
  check_eq("decode rejects len 28", decode_chem_status(d.data(), 28, st2), 0);
}

// IntelliChem 0x12 alarm/warning/dosing bytes (data[32..35]). Bit meanings
// from the firmware alarm namer FUN_008c1dfc; cross-checked vs njsPC byte map.
static void test_intellichem_alarms() {
  std::printf("[intellichem] alarm/warning/dosing decode (firmware FUN_008c1dfc + njsPC)\n");
  // A short (len 29) reply carries no alarm block: fields stay zero/false.
  std::vector<uint8_t> shortd(CHEM_STATUS_MIN_LEN, 0);
  ChemStatus s0;
  decode_chem_status(shortd.data(), shortd.size(), s0);
  check_eq("no alarm block when len 29", s0.has_alarms, 0);

  std::vector<uint8_t> d(CHEM_STATUS_ALARM_MIN_LEN, 0);
  // Alarms d[32]: No Flow (b0) + pH High (b1) + Probe Fault (b7).
  d[32] = CHEM_ALARM_NO_FLOW | CHEM_ALARM_PH_HIGH | CHEM_ALARM_PROBE_FAULT;
  // Warnings d[33]: pH Lockout (b0) + Peripheral Comm Error (b4).
  d[33] = CHEM_WARN_PH_LOCKOUT | CHEM_WARN_COMM_ERROR;
  // Dosing d[34] = 0x95 -> pH Monitoring (bits4-5 = 1), ORP Mixing (bits6-7 = 2).
  d[34] = 0x95;
  // Flags d[35] bit1 -> Flow Delay Active.
  d[35] = 0x02;

  ChemStatus st;
  check_eq("decode ok (len 36)", decode_chem_status(d.data(), d.size(), st), 1);
  check_eq("has_alarms", st.has_alarms, 1);
  check_str("alarms named", chem_alarm_string(st.alarms), "No Flow, pH High, Probe Fault");
  check_str("warnings named", chem_warning_string(st.warnings),
            "pH Lockout, Peripheral Comm Error");
  check_eq("pH dosing = Monitoring", st.ph_dosing, CHEM_DOSING_MONITORING);
  check_eq("ORP dosing = Mixing", st.orp_dosing, CHEM_DOSING_MIXING);
  check_str("pH dosing name", chem_dosing_status_name(st.ph_dosing), "Monitoring");
  check_str("ORP dosing name", chem_dosing_status_name(st.orp_dosing), "Mixing");
  check_eq("flow delay active", st.flow_delay, 1);

  // No active alarms/warnings -> empty strings.
  check_str("no alarms -> empty", chem_alarm_string(0), "");
  check_str("no warnings -> empty", chem_warning_string(0), "");
}

// IntelliChem 0x92 config-write payload. Byte layout proven from firmware
// binder FUN_0096f3bc (7 model tokens packed big-endian into a 21-byte payload,
// 12 meaningful bytes + 9 zero pad). Confirmed same command (0x92) and length
// (0x15) via IntelliChem_setupMessages CommBase_registerMessageTemplate.
static void test_intellichem_setpoint_write() {
  std::printf("[intellichem] 0x92 setpoint-write payload (firmware FUN_0096f3bc)\n");
  ChemConfig cfg;
  cfg.ph_setpoint_x100 = 760;    // 7.60 pH -> 0x02 0xF8
  cfg.orp_setpoint = 700;        // 700 mV -> 0x02 0xBC
  cfg.ph_tank = 5;
  cfg.orp_tank = 6;
  cfg.calcium_hardness = 400;    // 0x01 0x90
  cfg.cyanuric_acid = 50;        // 0x00 0x32
  cfg.total_alkalinity = 100;    // 0x00 0x64

  uint8_t p[CHEM_SET_PAYLOAD_LEN];
  build_chem_set_payload(cfg, p);
  std::vector<uint8_t> got(p, p + CHEM_SET_PAYLOAD_LEN);
  check_bytes("0x92 payload (BE16, 12 fields + 9 zero pad)", got,
              {0x02, 0xF8, 0x02, 0xBC, 0x05, 0x06, 0x01, 0x90, 0x00, 0x32, 0x00, 0x64,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Full A5 write frame: cmd 0x92, dest = IntelliChem 0x90, len 0x15.
  std::vector<uint8_t> f;
  build_a5_frame(f, 0x90, 0x10, CHEM_CMD_SET, p, CHEM_SET_PAYLOAD_LEN);
  check_eq("write cmd = 0x92", f[7], CHEM_CMD_SET);
  check_eq("write len = 0x15", f[8], CHEM_SET_PAYLOAD_LEN);
  check_eq("write dest = 0x90", f[5], 0x90);

  // The write side is the inverse of the read side: a value written as pH
  // setpoint 7.60 decodes back to 7.60 at the 0x12 status offset (d[4..5]).
  std::vector<uint8_t> d(CHEM_STATUS_MIN_LEN, 0);
  d[4] = p[0]; d[5] = p[1];
  ChemStatus st;
  decode_chem_status(d.data(), d.size(), st);
  check_close("pH setpoint round-trips 7.60", st.ph_setpoint, 7.60);
}

// heater named fault decode for the Fenwal gas heaters (MaxE-Therm,
// ETI250). Bit meanings from firmware alarm namer FUN_008c1dfc control flow.
static void test_heater_faults() {
  std::printf("[heater] named fault decode: ETI250 / MaxE-Therm (firmware FUN_008c1dfc)\n");
  // ETI250: ErrorFlagsA b4 = Ignition Control Value Signal, b6 = Thermal Fuse
  // Switch Error; FenwalDiag nibble 3 = Ignition Lockout.
  check_str("ETI250 errA b4|b6 + fenwal 3",
            heater_fault_string(HEATER_TYPE_ETI250, 0x50, 0x00, 0x03),
            "Ignition Control Value Signal, Thermal Fuse Switch Error, Ignition Lockout");
  // ETI250 ErrorFlagsB b4 = Replace Condensate Cartridge (ETI-only bit).
  check_str("ETI250 errB b4",
            heater_fault_string(HEATER_TYPE_ETI250, 0x00, 0x10, 0x00),
            "Replace Condensate Cartridge");
  // MaxE-Therm: ErrorFlagsA b4 = Ignition Control Error (different label than
  // ETI250); FenwalDiag nibble 4 = Weak Flame.
  check_str("MaxE-Therm errA b0|b4 + fenwal 4",
            heater_fault_string(HEATER_TYPE_MAXETHERM, 0x11, 0x00, 0x04),
            "Water Pressure Switch, Ignition Control Error, Weak Flame");
  // No faults -> empty string.
  check_str("MaxE-Therm no fault", heater_fault_string(HEATER_TYPE_MAXETHERM, 0, 0, 0), "");
  check_eq("ETI250/MaxE-Therm are named", heater_faults_named(HEATER_TYPE_ETI250), 1);

  // Heat pumps: Ultra faults are in wire d8 (aggregate B6) and d9 (B7); Hybrid
  // faults in wire d7/d8/d9. Proven end-to-end: parsers FUN_0097e234 /
  // FUN_0097c300 -> populator FUN_008c8ea4 -> namer FUN_008c1dfc.
  check_eq("Ultra is named", heater_faults_named(HEATER_TYPE_ULTRA), 1);
  check_eq("Hybrid is named", heater_faults_named(HEATER_TYPE_HYBRID), 1);
  check_eq("Ultra is heat pump", heater_is_heat_pump(HEATER_TYPE_ULTRA), 1);
  check_eq("MaxE-Therm is not heat pump", heater_is_heat_pump(HEATER_TYPE_MAXETHERM), 0);
  // Ultra d8 b0|b1 = Brownout, High Refrigerant; d9 b3 = Pool and Spa remote.
  check_str("Ultra d8 b0|b1 + d9 b3",
            heat_pump_fault_string(HEATER_TYPE_ULTRA, 0x00, 0x03, 0x08),
            "Brownout, High Refrigerant, Pool and Spa remote inputs are both enabled");
  // Ultra d9 b7 = Defrost Temp Sensor Shorted (top bit); d7 is ignored for Ultra.
  check_str("Ultra d9 b7 (d7 ignored)",
            heat_pump_fault_string(HEATER_TYPE_ULTRA, 0xFF, 0x00, 0x80),
            "Defrost Temp Sensor Shorted");
  // Hybrid d7 b0 = Air Flow Switch; d8 b7 = High Limit Switch; d9 b4 = Suction
  // Temperature Sensor Open/Short.
  check_str("Hybrid d7 b0 + d8 b7 + d9 b4",
            heat_pump_fault_string(HEATER_TYPE_HYBRID, 0x01, 0x80, 0x10),
            "Air Flow Switch, High Limit Switch, Suction Temperature Sensor Open/Short");
  // No alarm bits -> empty.
  check_str("Ultra no fault", heat_pump_fault_string(HEATER_TYPE_ULTRA, 0, 0, 0), "");
}

// heat-pump command payloads. ULTRA (setUltraParams FUN_0097d428):
// payload[0]=0x90 marker, payload[1]=mode (0 Off / 1 Heating / 2 Cooling, model
// token 0xeda8), payload[9]=service. HYBRID (FUN_0097b558) uses a DIFFERENT
// layout with NO marker: [0]=mode, [1]=heatingMode, [2]=Set Point, [3]=the
// 0xf38c byte (clamped [5,60], firmware default 15), [4]=boostTemp, [9]=service.
// Both pack into the poll frame (Ultra cmd 0x72 / Hybrid cmd 0x70, len 10) and
// the library emits them as the OCP replacement / bus master.
static void test_heat_pump_command() {
  std::printf("[heater] Ultra + Hybrid command payloads (firmware FUN_0097d428 / FUN_0097b558)\n");
  check_eq("mode Off = 0", HEAT_PUMP_MODE_OFF, 0);
  check_eq("mode Heating = 1", HEAT_PUMP_MODE_HEATING, 1);
  check_eq("mode Cooling = 2", HEAT_PUMP_MODE_COOLING, 2);
  check_eq("marker = 0x90", ULTRA_CMD_MARKER, 0x90);
  check_str("mode name Off", heat_pump_mode_name(HEAT_PUMP_MODE_OFF), "Off");
  check_str("mode name Heating", heat_pump_mode_name(HEAT_PUMP_MODE_HEATING), "Heating");
  check_str("mode name Cooling", heat_pump_mode_name(HEAT_PUMP_MODE_COOLING), "Cooling");

  // Both heat pumps carry a proven bus command; gas heaters do not.
  check_eq("Ultra supports bus command", heater_supports_bus_command(HEATER_TYPE_ULTRA), 1);
  check_eq("Hybrid supports bus command", heater_supports_bus_command(HEATER_TYPE_HYBRID), 1);
  check_eq("MasterTemp excluded", heater_supports_bus_command(HEATER_TYPE_MASTERTEMP), 0);

  const uint8_t len = heater_request_len(HEATER_TYPE_ULTRA);  // 10
  uint8_t p[16];

  // Cooling, no service: [0]=0x90, [1]=0x02, rest 0.
  build_ultra_cmd_payload(HEAT_PUMP_MODE_COOLING, false, p, len);
  check_bytes("Ultra cooling, no service", std::vector<uint8_t>(p, p + len),
              {0x90, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Heating + service: [1]=0x01, [9]=0x01.
  build_ultra_cmd_payload(HEAT_PUMP_MODE_HEATING, true, p, len);
  check_bytes("Ultra heating + service", std::vector<uint8_t>(p, p + len),
              {0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01});

  // Off: marker still present, mode 0.
  build_ultra_cmd_payload(HEAT_PUMP_MODE_OFF, false, p, len);
  check_bytes("Ultra off", std::vector<uint8_t>(p, p + len),
              {0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Full A5 command frame: Ultra @0x72, cmd 0x72, len 10, cooling payload.
  // sum(A5 00 72 10 72 0A 90 02 + eight 00) = 0x0235 -> 02 35.
  build_ultra_cmd_payload(HEAT_PUMP_MODE_COOLING, false, p, len);
  std::vector<uint8_t> f;
  build_a5_frame(f, 0x72, 0x10, heater_request_cmd(HEATER_TYPE_ULTRA), p, len);
  check_bytes("Ultra cooling command frame @0x72", f,
              {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x72, 0x10, 0x72, 0x0A, 0x90, 0x02, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x35});

  // ---- Hybrid command (FUN_0097b558) ----
  check_eq("hybrid heat unset = 0", HYBRID_HEAT_UNSET, 0);
  check_eq("hybrid heat pump only = 1", HYBRID_HEAT_PUMP_ONLY, 1);
  check_eq("hybrid dual = 4", HYBRID_HEAT_DUAL, 4);
  check_eq("hybrid param default = 15", HYBRID_PARAM_DEFAULT, 15);
  check_eq("hybrid setpoint default = 78", HYBRID_SETPOINT_DEFAULT, 78);
  check_str("hybrid mode name", hybrid_heating_mode_name(HYBRID_HEAT_HYBRID), "Hybrid Mode");
  check_str("hybrid dual name", hybrid_heating_mode_name(HYBRID_HEAT_DUAL), "Dual Mode");

  const uint8_t hlen = heater_request_len(HEATER_TYPE_HYBRID);  // 10
  // Heating, Hybrid Mode, setpoint 78, param 15, boost 10, no service.
  build_hybrid_cmd_payload(HEAT_PUMP_MODE_HEATING, HYBRID_HEAT_HYBRID, 78, 15, 10, false, p, hlen);
  check_bytes("Hybrid heating/hybrid-mode", std::vector<uint8_t>(p, p + hlen),
              {0x01, 0x03, 0x4E, 0x0F, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00});

  // param out of range is clamped to [5,60] exactly like the firmware setter.
  build_hybrid_cmd_payload(HEAT_PUMP_MODE_OFF, HYBRID_HEAT_GAS_ONLY, 80, 100, 0, false, p, hlen);
  check_eq("Hybrid param clamped high -> 60", p[HYBRID_CMD_OFF_PARAM], 60);
  build_hybrid_cmd_payload(HEAT_PUMP_MODE_OFF, HYBRID_HEAT_GAS_ONLY, 80, 2, 0, false, p, hlen);
  check_eq("Hybrid param clamped low -> 5", p[HYBRID_CMD_OFF_PARAM], 5);

  // service sets payload[9].
  build_hybrid_cmd_payload(HEAT_PUMP_MODE_COOLING, HYBRID_HEAT_DUAL, 60, 20, 5, true, p, hlen);
  check_bytes("Hybrid cooling/dual + service", std::vector<uint8_t>(p, p + hlen),
              {0x02, 0x04, 0x3C, 0x14, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01});

  // Full A5 Hybrid frame @0x70, cmd 0x70: sum(A5 00 70 10 70 0A 01 03 4E 0F 0A)
  // = 0x020A -> 02 0A.
  build_hybrid_cmd_payload(HEAT_PUMP_MODE_HEATING, HYBRID_HEAT_HYBRID, 78, 15, 10, false, p, hlen);
  std::vector<uint8_t> hf;
  build_a5_frame(hf, 0x70, 0x10, heater_request_cmd(HEATER_TYPE_HYBRID), p, hlen);
  check_bytes("Hybrid heating command frame @0x70", hf,
              {0xFF, 0x00, 0xFF, 0xA5, 0x00, 0x70, 0x10, 0x70, 0x0A, 0x01, 0x03, 0x4E, 0x0F,
               0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0A});
}

// pump 0x07 status decode. Run-state (d[0]/d[2]) and alarm word (d[9..10])
// semantics proven from IntelliFloVSF_decodeStatus07 / logStatusPacket.
static void test_pump_status_decode() {
  std::printf("[pump] 0x07 status decode: state + telemetry + alarms (firmware)\n");
  // Running pump: d[0]=0x0A, watts d[3..4]=170, rpm d[5..6]=1250, gpm d[7]=0.
  std::vector<uint8_t> d(11, 0);
  d[0] = 0x0A;              // running
  d[2] = 0;                // not priming
  d[3] = 0x00; d[4] = 0xAA; // watts 170
  d[5] = 0x04; d[6] = 0xE2; // rpm 1250
  d[7] = 0;                // gpm
  PumpStatus st;
  check_eq("decode ok", decode_pump_status(d.data(), d.size(), st), 1);
  check_eq("state = RUNNING", st.state, PUMP_STATE_RUNNING);
  check_eq("watts = 170", st.watts, 170);
  check_eq("rpm = 1250", st.rpm, 1250);
  check_eq("no alarms", st.alarms, 0);

  // Priming: d[0]=0x0A and d[2]==1.
  d[2] = 1;
  decode_pump_status(d.data(), d.size(), st);
  check_eq("state = PRIMING", st.state, PUMP_STATE_PRIMING);

  // Stopped: d[0]=0x04.
  d[0] = PUMP_STOP; d[2] = 0;
  decode_pump_status(d.data(), d.size(), st);
  check_eq("state = STOPPED", st.state, PUMP_STATE_STOPPED);

  // Alarm word W = (d[9]<<8)|d[10]. OVER_TEMP = W bit 6 -> d[10] bit 6 (0x40).
  d[0] = 0x0A; d[9] = 0x00; d[10] = 0x40;
  decode_pump_status(d.data(), d.size(), st);
  check_eq("OVER_TEMP set", (st.alarms & PUMP_ALARM_OVER_TEMP) != 0, 1);
  check_eq("HIGH_TEMP clear", (st.alarms & PUMP_ALARM_HIGH_TEMP) != 0, 0);
  // OVER_CURRENT = W bit 8 -> d[9] bit 0 (0x01).
  d[9] = 0x01; d[10] = 0x00;
  decode_pump_status(d.data(), d.size(), st);
  check_eq("OVER_CURRENT set", (st.alarms & PUMP_ALARM_OVER_CURRENT) != 0, 1);
  // HIGH_TEMP = W bit 2 -> d[10] bit 2 (0x04).
  d[9] = 0x00; d[10] = 0x04;
  decode_pump_status(d.data(), d.size(), st);
  check_eq("HIGH_TEMP set", (st.alarms & PUMP_ALARM_HIGH_TEMP) != 0, 1);
}

int main() {
  std::printf("Pentair protocol golden-vector tests\n");
  std::printf("(validated against the firmware-derived protocol spec)\n\n");
  test_constants();
  test_a5_framing();
  test_chlor_framing();
  test_chlor_status_decode();
  test_setpoint_encoding();
  test_pump_status_decode();
  test_heater();
  test_intellichem();
  test_intellichem_status_decode();
  test_intellichem_alarms();
  test_intellichem_setpoint_write();
  test_heater_faults();
  test_heat_pump_command();
  std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0: 1;
}
