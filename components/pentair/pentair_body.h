#pragma once
#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "pentair_circuit.h"

namespace esphome {
namespace pentair {

// A pool/spa "body" thermostat for a GAS heater.
//
// This mirrors how the IntelliCenter OCP controls a GAS heater (MasterTemp,
// MaxE-Therm, ETI250), which is NOT an RS-485 command to the heater. A gas
// heater is polled for status only (see the protocol overview in README.md): its
// request payload carries no heat demand. Instead the OCP compares the body
// WATER TEMPERATURE against the configured heat setpoint / heat mode and
// energises a heat-source ("fireman") relay output. In the firmware the water
// temperature is read from a LOCAL analog sensor via ADC (with calibration) --
// not from the bus -- so here it comes from a configured ESPHome sensor, and
// the heat-source output is a PentairFeatureCircuit relay.
//
// NOTE: this relay model applies to GAS heaters only. The Ultra/Hybrid heat
// pumps ARE commanded directly over the bus (mode + service in their poll
// frame); that path lives in PentairHeater, not here.
//
// The OCP also requires a body circuit to be running before it will call for
// heat ("No Body Circuit is Running! Can't Process Heater Change!"); the
// optional body_circuit models that interlock.
class PentairBody: public climate::Climate, public Component {
 public:
  void set_temperature_sensor(sensor::Sensor *s) { this->temp_sensor_ = s; }
  void set_heat_circuit(PentairFeatureCircuit *c) { this->heat_circuit_ = c; }
  void set_body_circuit(PentairFeatureCircuit *c) { this->body_circuit_ = c; }
  void set_hysteresis(float h) { this->hysteresis_ = h; }
  void set_visual_min(float v) { this->visual_min_ = v; }
  void set_visual_max(float v) { this->visual_max_ = v; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;
  // Recompute the call-for-heat decision and drive the heat-source relay.
  // Returns true if anything changed (mode/action/heat state).
  bool update_();

  sensor::Sensor *temp_sensor_{nullptr};
  PentairFeatureCircuit *heat_circuit_{nullptr};
  PentairFeatureCircuit *body_circuit_{nullptr};
  float hysteresis_{0.5f};
  float visual_min_{18.0f};
  float visual_max_{40.0f};
  bool calling_for_heat_{false};
  uint32_t last_eval_{0};
};

}  // namespace pentair
}  // namespace esphome
