#pragma once
#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace pentair {

// IntelliChlor salt water generator. Owns the requested output %, and reports
// salt / alarms. The effective output that is actually commanded onto the bus
// is flow-gated by the hub (mirrors the firmware boolean flow gate).
class PentairChlorinator: public Component {
 public:
  void dump_config() override;

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t address() const { return this->address_; }
  bool comm_ok() const { return this->comm_ok_; }

  // ---- entity hookups ----
  void set_salt_sensor(sensor::Sensor *s) { this->salt_sensor_ = s; }
  void set_comm_binary_sensor(binary_sensor::BinarySensor *s) { this->comm_bs_ = s; }
  void set_flow_binary_sensor(binary_sensor::BinarySensor *s) { this->flow_bs_ = s; }    void set_no_flow_binary_sensor(binary_sensor::BinarySensor *s) { this->no_flow_bs_ = s; }  void set_low_salt_binary_sensor(binary_sensor::BinarySensor *s) { this->low_salt_bs_ = s; }
  void set_very_low_salt_binary_sensor(binary_sensor::BinarySensor *s) { this->very_low_salt_bs_ = s; }
  void set_clean_cell_binary_sensor(binary_sensor::BinarySensor *s) { this->clean_cell_bs_ = s; }
  void set_cold_water_binary_sensor(binary_sensor::BinarySensor *s) { this->cold_water_bs_ = s; }

  // ---- control (from number entity) ----
  void set_output(uint8_t percent) { this->requested_output_ = percent > 100 ? 100: percent; }
  uint8_t requested_output() const { return this->requested_output_; }

  // ---- RX callback ----
  void on_status(uint8_t salt_raw, uint8_t alarms);

  // Presence: the hub marks the cell connected when it answers a poll and
  // disconnected when it goes silent (mirrors the firmware Connected/
  // Disconnected routing-table status).
  void mark_comm(bool ok);

  // Called by the hub each command cycle with the resolved flow-gate state.
  void on_flow_gate(bool present);

 protected:
  uint8_t address_{ADDR_CHLOR_DEFAULT};
  uint8_t requested_output_{0};
  bool comm_ok_{false};

  sensor::Sensor *salt_sensor_{nullptr};
  binary_sensor::BinarySensor *comm_bs_{nullptr};
  binary_sensor::BinarySensor *flow_bs_{nullptr};    binary_sensor::BinarySensor *no_flow_bs_{nullptr};  binary_sensor::BinarySensor *low_salt_bs_{nullptr};
  binary_sensor::BinarySensor *very_low_salt_bs_{nullptr};
  binary_sensor::BinarySensor *clean_cell_bs_{nullptr};
  binary_sensor::BinarySensor *cold_water_bs_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
