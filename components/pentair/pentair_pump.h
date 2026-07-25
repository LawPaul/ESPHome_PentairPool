#pragma once
#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace pentair {

// Represents a single IntelliFlo pump on the bus. Owns the pump's desired
// state (setpoint, run) and its last-reported telemetry, and exposes the
// firmware-correct command payloads for its pump type.
class PentairPump : public Component {
 public:
  void dump_config() override;

  void set_address(uint8_t address) { this->address_ = address; }
  void set_pump_type(PumpType type) { this->type_ = type; }
  void set_mode(PumpMode mode) { this->mode_ = mode; }

  uint8_t address() const { return this->address_; }
  PumpType pump_type() const { return this->type_; }
  bool running() const { return this->running_; }
  bool comm_ok() const { return this->comm_ok_; }

  // ---- entity hookups (set by platform components) ----
  void set_rpm_sensor(sensor::Sensor *s) { this->rpm_sensor_ = s; }
  void set_gpm_sensor(sensor::Sensor *s) { this->gpm_sensor_ = s; }
  void set_watts_sensor(sensor::Sensor *s) { this->watts_sensor_ = s; }
  void set_comm_binary_sensor(binary_sensor::BinarySensor *s) { this->comm_bs_ = s; }
  void set_priming_binary_sensor(binary_sensor::BinarySensor *s) { this->priming_bs_ = s; }
  void set_alarm_binary_sensor(binary_sensor::BinarySensor *s) { this->alarm_bs_ = s; }
  void set_high_temp_binary_sensor(binary_sensor::BinarySensor *s) { this->high_temp_bs_ = s; }
  void set_prime_error_binary_sensor(binary_sensor::BinarySensor *s) { this->prime_error_bs_ = s; }
  void set_over_temp_binary_sensor(binary_sensor::BinarySensor *s) { this->over_temp_bs_ = s; }
  void set_power_error_binary_sensor(binary_sensor::BinarySensor *s) { this->power_error_bs_ = s; }
  void set_over_current_binary_sensor(binary_sensor::BinarySensor *s) { this->over_current_bs_ = s; }
  void set_over_voltage_binary_sensor(binary_sensor::BinarySensor *s) { this->over_voltage_bs_ = s; }
  void set_unknown_alarm_binary_sensor(binary_sensor::BinarySensor *s) { this->unknown_alarm_bs_ = s; }

  // ---- control (called from number / switch entities) ----
  // A change marks the pump "pending" so the hub emits command frames and
  // speeds up its poll cadence, mirroring the OCP's event-driven behaviour.
  void set_target(uint16_t value) {
    if (value != this->target_)
      this->pending_ = true;
    this->target_ = value;
  }
  void set_run(bool run) {
    if (run != this->run_)
      this->pending_ = true;
    this->run_ = run;
  }
  uint16_t target() const { return this->target_; }
  bool run_requested() const { return this->run_; }

  // "Active" = running, commanded to run, or a fresh setpoint/run change is
  // pending. The OCP polls active pumps at 2 s and idle pumps at 16 s, and
  // only emits command frames while active.
  bool is_active() const { return this->run_ || this->running_ || this->pending_; }
  void clear_pending() { this->pending_ = false; }

  // ---- the desired setpoint command, clamped to the pump's range ----
  // Fills cmd/data for the setpoint write appropriate to this pump type.
  void build_setpoint(uint8_t &cmd, std::vector<uint8_t> &data) const;

  // ---- RX callbacks (called by the hub) ----
  void on_status(const PumpStatus &st);
  void mark_comm(bool ok);

 protected:
  uint16_t clamp_setpoint_() const;

  uint8_t address_{ADDR_PUMP_BASE};
  PumpType type_{PUMP_TYPE_VSF};
  PumpMode mode_{PUMP_MODE_SPEED};

  uint16_t target_{0};
  bool run_{false};
  bool running_{false};
  bool comm_ok_{false};
  bool pending_{false};

  sensor::Sensor *rpm_sensor_{nullptr};
  sensor::Sensor *gpm_sensor_{nullptr};
  sensor::Sensor *watts_sensor_{nullptr};
  binary_sensor::BinarySensor *comm_bs_{nullptr};
  binary_sensor::BinarySensor *priming_bs_{nullptr};
  binary_sensor::BinarySensor *alarm_bs_{nullptr};
  binary_sensor::BinarySensor *high_temp_bs_{nullptr};
  binary_sensor::BinarySensor *prime_error_bs_{nullptr};
  binary_sensor::BinarySensor *over_temp_bs_{nullptr};
  binary_sensor::BinarySensor *power_error_bs_{nullptr};
  binary_sensor::BinarySensor *over_current_bs_{nullptr};
  binary_sensor::BinarySensor *over_voltage_bs_{nullptr};
  binary_sensor::BinarySensor *unknown_alarm_bs_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
