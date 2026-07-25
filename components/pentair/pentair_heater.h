#pragma once
#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace pentair {

// Pentair heater (Ultra / Hybrid / MasterTemp / MaxE-Therm / ETI250).
//
// The OCP polls each heater on a fixed per-subtype cadence and parses a status
// reply (heaterMode, HeaterStatus, ErrorFlagsA/B, FenwalDiag).
//
// GAS heaters (MasterTemp, MaxE-Therm, ETI250): the status-request payload is
// NOT decoded in firmware (their setup binds no payload builder), so they
// are read-only telemetry -- polled zero-filled, reply parsed. Heat demand for
// a gas heater is driven by body setpoint/relay logic (see PentairBody).
//
// HEAT PUMPS (Ultra, Hybrid): the firmware DOES bind a payload builder that
// packs a command into the poll frame, and this library -- as the OCP
// replacement / sole bus master -- emits it from its own entities. The ULTRA
// layout (setUltraParams: payload[0]=0x90 marker, payload[1]=mode 0 Off /
// 1 Heating / 2 Cooling, payload[9]=service) and the HYBRID layout (FUN_0097b558:
// payload[0]=mode, [1]=heatingMode, [2]=Set Point, [3]=a [5,60] tunable that
// defaults to the firmware's 15, [4]=boostTemp, [9]=service) are both firmware-
// proven. Until a mode is commanded the frame stays zero-filled like a gas
// heater (see set_heat_pump_mode / fill_request_payload).
class PentairHeater: public Component {
 public:
  void dump_config() override;

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t address() const { return this->address_; }
  void set_heater_type(HeaterType type) { this->type_ = type; }
  HeaterType heater_type() const { return this->type_; }
  bool comm_ok() const { return this->comm_ok_; }

  // Firmware poll cadence + request framing for this subtype.
  uint32_t poll_interval_ms() const { return heater_poll_interval_ms(this->type_); }
  uint8_t request_cmd() const { return heater_request_cmd(this->type_); }
  uint8_t request_len() const { return heater_request_len(this->type_); }
  // True if an inbound reply command belongs to this heater's subtype.
  bool matches_reply_cmd(uint8_t cmd) const;

  // ---- heat-pump command (Ultra + Hybrid) ----
  // Opt-in: until a mode is commanded the poll frame stays zero-filled (like a
  // gas heater). Commanding a mode on a subtype without a proven bus command
  // (the gas heaters) is ignored.
  bool is_heat_pump() const { return heater_is_heat_pump(this->type_); }
  bool supports_bus_command() const { return heater_supports_bus_command(this->type_); }
  void set_heat_pump_mode(HeatPumpMode mode) {
    if (!this->supports_bus_command())
      return;
    this->hp_mode_ = mode;
    this->hp_control_active_ = true;
  }
  HeatPumpMode heat_pump_mode() const { return this->hp_mode_; }
  bool heat_pump_control_active() const { return this->hp_control_active_; }

  // Hybrid-only fields (ignored by the Ultra builder). heating_mode is the
  // heat-source enum; set_point is the target water temp (typically driven from
  // the body climate); boost_temp is the gas boost; param is the payload[3]
  // byte (token 0xf38c), clamped [5,60] exactly like the firmware setter.
  void set_hybrid_heating_mode(HybridHeatingMode m) { this->hp_heating_mode_ = m; }
  HybridHeatingMode hybrid_heating_mode() const { return this->hp_heating_mode_; }
  void set_hybrid_set_point(uint8_t sp) { this->hp_set_point_ = sp; }
  void set_hybrid_boost_temp(uint8_t b) { this->hp_boost_temp_ = b; }
  void set_hybrid_param(uint8_t p) {
    this->hp_param_ = p < HYBRID_PARAM_MIN ? HYBRID_PARAM_MIN
                                           : (p > HYBRID_PARAM_MAX ? HYBRID_PARAM_MAX: p);
  }

  // Fill this poll's request payload. Returns true and writes the Ultra or
  // Hybrid command once the user has commanded a mode; otherwise leaves out
  // untouched (the hub sends it zero-filled).
  bool fill_request_payload(uint8_t *out, uint8_t len) const {
    if (!this->hp_control_active_)
      return false;
    if (this->type_ == HEATER_TYPE_HYBRID) {
      build_hybrid_cmd_payload(this->hp_mode_, this->hp_heating_mode_, this->hp_set_point_,
                               this->hp_param_, this->hp_boost_temp_, this->hp_service_, out, len);
    } else {
      build_ultra_cmd_payload(this->hp_mode_, this->hp_service_, out, len);
    }
    return true;
  }

  // ---- entity hookups ----
  void set_mode_sensor(sensor::Sensor *s) { this->mode_sensor_ = s; }
  void set_status_sensor(sensor::Sensor *s) { this->status_sensor_ = s; }
  void set_error_a_sensor(sensor::Sensor *s) { this->error_a_sensor_ = s; }
  void set_error_b_sensor(sensor::Sensor *s) { this->error_b_sensor_ = s; }
  void set_fenwal_sensor(sensor::Sensor *s) { this->fenwal_sensor_ = s; }
  void set_comm_binary_sensor(binary_sensor::BinarySensor *s) { this->comm_bs_ = s; }
  void set_fault_binary_sensor(binary_sensor::BinarySensor *s) { this->fault_bs_ = s; }
  void set_status_text_sensor(text_sensor::TextSensor *s) { this->status_ts_ = s; }
  void set_fault_text_sensor(text_sensor::TextSensor *s) { this->fault_ts_ = s; }

  // ---- RX callback: full A5 data payload of the heater status reply ----
  void on_status(const std::vector<uint8_t> &data);

  // Presence: connected on reply, disconnected after prolonged silence.
  void mark_comm(bool ok);

 protected:
  uint8_t address_{0x70};
  HeaterType type_{HEATER_TYPE_MASTERTEMP};
  bool comm_ok_{false};

  // Heat-pump command state (Ultra / Hybrid only).
  bool hp_control_active_{false};
  HeatPumpMode hp_mode_{HEAT_PUMP_MODE_OFF};
  bool hp_service_{false};
  // Hybrid-only extra command fields (defaults match the firmware where proven:
  // set_point 0x4e/78 fallback, param 0xf38c init 15; heating_mode defaults to
  // "Hybrid Mode" for a hybrid unit and is user-overridable, boost 0).
  HybridHeatingMode hp_heating_mode_{HYBRID_HEAT_HYBRID};
  uint8_t hp_set_point_{HYBRID_SETPOINT_DEFAULT};
  uint8_t hp_boost_temp_{0};
  uint8_t hp_param_{HYBRID_PARAM_DEFAULT};

  sensor::Sensor *mode_sensor_{nullptr};
  sensor::Sensor *status_sensor_{nullptr};
  sensor::Sensor *error_a_sensor_{nullptr};
  sensor::Sensor *error_b_sensor_{nullptr};
  sensor::Sensor *fenwal_sensor_{nullptr};
  binary_sensor::BinarySensor *comm_bs_{nullptr};
  binary_sensor::BinarySensor *fault_bs_{nullptr};
  text_sensor::TextSensor *status_ts_{nullptr};
  text_sensor::TextSensor *fault_ts_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
