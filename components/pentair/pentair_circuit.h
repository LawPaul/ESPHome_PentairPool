#pragma once
#include <cmath>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace pentair {

// A "feature circuit": a local relay output the ESP drives directly.
//
// This mirrors how the IntelliCenter OCP controls its AUX / feature-circuit
// relays (pool, spa, lights, cleaner, water features, the heater "fireman"
// contact, single-speed pumps). Those relays are switched by LOCAL hardware on
// the controller board over its internal I2C relay bus, keyed by feature-circuit
// index -- they are NOT an RS-485 command (the bus only broadcasts relay status
// to remote panels; see the "Relays / feature circuits" note in README.md). So
// this class does not touch the Pentair
// bus at all; it drives an ESP GPIO relay, with the same optional behaviours a
// feature circuit has on the OCP: an egg-timer auto-off and freeze protection.
class PentairFeatureCircuit : public switch_::Switch, public Component {
 public:
  void set_pin(GPIOPin *pin) { this->pin_ = pin; }
  void set_egg_timer(uint32_t seconds) { this->egg_timer_s_ = seconds; }
  void set_freeze_sensor(sensor::Sensor *s) { this->freeze_sensor_ = s; }
  void set_freeze_threshold(float t) { this->freeze_threshold_ = t; }

  // True while freeze protection is forcing the relay on.
  bool freeze_active() const { return this->freeze_active_; }
  // The user-requested (schedule/manual) state, ignoring freeze override.
  bool desired() const { return this->desired_; }

  void setup() override {
    this->pin_->setup();
    this->pin_->digital_write(false);
  }

  void loop() override {
    // Egg-timer auto-off: release the manual request after the configured run.
    if (this->desired_ && this->egg_timer_s_ > 0 &&
        (millis() - this->on_since_) >= this->egg_timer_s_ * 1000UL) {
      this->desired_ = false;
    }
    this->refresh_();
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void dump_config() override {
    const char *const TAG = "pentair.circuit";
    LOG_SWITCH("", "Pentair Feature Circuit", this);
    if (this->egg_timer_s_ > 0)
      ESP_LOGCONFIG("pentair.circuit", "  Egg timer: %u s", this->egg_timer_s_);
    if (this->freeze_sensor_ != nullptr)
      ESP_LOGCONFIG("pentair.circuit", "  Freeze protection when temp <= %.1f",
                    this->freeze_threshold_);
  }

 protected:
  void write_state(bool state) override {
    this->desired_ = state;
    if (state)
      this->on_since_ = millis();
    this->refresh_();
  }

  void refresh_() {
    this->freeze_active_ = false;
    if (this->freeze_sensor_ != nullptr && this->freeze_sensor_->has_state() &&
        !std::isnan(this->freeze_sensor_->state)) {
      this->freeze_active_ = this->freeze_sensor_->state <= this->freeze_threshold_;
    }
    bool on = this->desired_ || this->freeze_active_;
    this->pin_->digital_write(on);
    if (on != this->state)
      this->publish_state(on);
  }

  GPIOPin *pin_{nullptr};
  sensor::Sensor *freeze_sensor_{nullptr};
  bool desired_{false};
  bool freeze_active_{false};
  uint32_t on_since_{0};
  uint32_t egg_timer_s_{0};
  float freeze_threshold_{4.4f};  // ~40 F; overridden from config when used
};

}  // namespace pentair
}  // namespace esphome
