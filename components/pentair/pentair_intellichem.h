#pragma once
#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace pentair {

// Pentair IntelliChem controller.
//
// [E] The OCP polls IntelliChem every 30 s with cmd 0xd2 (single payload byte
// 0xD2) and the device answers with cmd 0x12. The 0x12 reply is parsed by the
// firmware status handler FUN_0096fed0, whose own debug string names each
// field; the wire layout is decoded in protocol.h (decode_chem_status).
// This device performs the poll, tracks presence/comm, publishes the decoded
// chemistry, and also surfaces the raw reply payload as a diagnostic sensor.
class PentairIntelliChem: public Component {
 public:
  void dump_config() override;

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t address() const { return this->address_; }
  bool comm_ok() const { return this->comm_ok_; }

  uint32_t poll_interval_ms() const { return CHEM_POLL_INTERVAL_MS; }

  // ---- entity hookups ----
  void set_comm_binary_sensor(binary_sensor::BinarySensor *s) { this->comm_bs_ = s; }
  void set_reply_text_sensor(text_sensor::TextSensor *s) { this->reply_ts_ = s; }
  void set_alarms_text_sensor(text_sensor::TextSensor *s) { this->alarms_ts_ = s; }
  void set_warnings_text_sensor(text_sensor::TextSensor *s) { this->warnings_ts_ = s; }
  void set_ph_dosing_text_sensor(text_sensor::TextSensor *s) { this->ph_dosing_ts_ = s; }
  void set_orp_dosing_text_sensor(text_sensor::TextSensor *s) { this->orp_dosing_ts_ = s; }
  void set_ph_sensor(sensor::Sensor *s) { this->ph_sensor_ = s; }
  void set_orp_sensor(sensor::Sensor *s) { this->orp_sensor_ = s; }
  void set_ph_setpoint_sensor(sensor::Sensor *s) { this->ph_setpoint_sensor_ = s; }
  void set_orp_setpoint_sensor(sensor::Sensor *s) { this->orp_setpoint_sensor_ = s; }
  void set_ph_tank_sensor(sensor::Sensor *s) { this->ph_tank_sensor_ = s; }
  void set_orp_tank_sensor(sensor::Sensor *s) { this->orp_tank_sensor_ = s; }
  void set_saturation_index_sensor(sensor::Sensor *s) { this->saturation_index_sensor_ = s; }
  void set_calcium_hardness_sensor(sensor::Sensor *s) { this->calcium_hardness_sensor_ = s; }
  void set_cyanuric_acid_sensor(sensor::Sensor *s) { this->cyanuric_acid_sensor_ = s; }
  void set_total_alkalinity_sensor(sensor::Sensor *s) { this->total_alkalinity_sensor_ = s; }

  // ---- RX callback: full A5 data payload of the 0x12 status reply ----
  void on_status(const std::vector<uint8_t> &data);

  // Presence: connected on reply, disconnected after prolonged silence.
  void mark_comm(bool ok);

  // ---- setpoint writes (0x92 config-write) ----
  // Each setter merges one field into the last-known device config and arms a
  // pending write. Writes are refused until a status reply has been decoded, so
  // the untouched fields are always resent with their current values rather
  // than clobbered with zeros.
  void set_ph_setpoint(float ph);          // pH units, e.g. 7.2
  void set_orp_setpoint(float mv);         // ORP, mV
  void set_calcium_hardness(float ppm);    // LSI input
  void set_cyanuric_acid(float ppm);       // LSI input
  void set_total_alkalinity(float ppm);    // LSI input

  // Hub hook: if a write is armed, fills payload (CHEM_SET_PAYLOAD_LEN bytes)
  // and clears the pending flag. Returns false when nothing is queued.
  bool take_pending_write(uint8_t *payload);

 protected:
  uint8_t address_{0x90};
  bool comm_ok_{false};

  // Setpoint-write state: cfg_ tracks the last decoded device config; pending_
  // accumulates user edits until the hub emits them.
  ChemConfig cfg_{};
  ChemConfig pending_{};
  bool have_config_{false};
  bool write_pending_{false};
  void begin_edit_();  // seed pending_ from cfg_ when starting a fresh edit

  binary_sensor::BinarySensor *comm_bs_{nullptr};
  text_sensor::TextSensor *reply_ts_{nullptr};
  text_sensor::TextSensor *alarms_ts_{nullptr};
  text_sensor::TextSensor *warnings_ts_{nullptr};
  text_sensor::TextSensor *ph_dosing_ts_{nullptr};
  text_sensor::TextSensor *orp_dosing_ts_{nullptr};
  sensor::Sensor *ph_sensor_{nullptr};
  sensor::Sensor *orp_sensor_{nullptr};
  sensor::Sensor *ph_setpoint_sensor_{nullptr};
  sensor::Sensor *orp_setpoint_sensor_{nullptr};
  sensor::Sensor *ph_tank_sensor_{nullptr};
  sensor::Sensor *orp_tank_sensor_{nullptr};
  sensor::Sensor *saturation_index_sensor_{nullptr};
  sensor::Sensor *calcium_hardness_sensor_{nullptr};
  sensor::Sensor *cyanuric_acid_sensor_{nullptr};
  sensor::Sensor *total_alkalinity_sensor_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
