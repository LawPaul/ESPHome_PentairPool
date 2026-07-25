#include "pentair_body.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.body";

void PentairBody::setup() {
  this->mode = climate::CLIMATE_MODE_OFF;
  this->action = climate::CLIMATE_ACTION_OFF;
  this->target_temperature = (this->visual_min_ + this->visual_max_) / 2.0f;
  if (this->temp_sensor_ != nullptr) {
    if (this->temp_sensor_->has_state())
      this->current_temperature = this->temp_sensor_->state;
    this->temp_sensor_->add_on_state_callback([this](float v) {
      this->current_temperature = v;
      if (this->update_())
        this->publish_state();
    });
  }
  this->publish_state();
}

void PentairBody::loop() {
  uint32_t now = millis();
  // Re-evaluate periodically so body-circuit changes and egg-timer releases are
  // picked up even without a new temperature reading.
  if (now - this->last_eval_ < 1000)
    return;
  this->last_eval_ = now;
  if (this->temp_sensor_ != nullptr && this->temp_sensor_->has_state())
    this->current_temperature = this->temp_sensor_->state;
  if (this->update_())
    this->publish_state();
}

bool PentairBody::update_() {
  bool want_heat = false;
  if (this->mode == climate::CLIMATE_MODE_HEAT) {
    // Interlock: the OCP will not call for heat unless the body circuit is on.
    bool body_on = (this->body_circuit_ == nullptr) || this->body_circuit_->desired();
    float cur = this->current_temperature;
    if (body_on && !std::isnan(cur)) {
      if (this->calling_for_heat_) {
        // Hold heat until the setpoint is reached.
        want_heat = cur < this->target_temperature;
      } else {
        // Start heating once we fall a full hysteresis band below setpoint.
        want_heat = cur <= (this->target_temperature - this->hysteresis_);
      }
    }
  }

  bool changed = false;
  if (want_heat != this->calling_for_heat_) {
    this->calling_for_heat_ = want_heat;
    if (this->heat_circuit_ != nullptr) {
      if (want_heat)
        this->heat_circuit_->turn_on();
      else
        this->heat_circuit_->turn_off();
    }
    changed = true;
  }

  climate::ClimateAction new_action;
  if (this->mode != climate::CLIMATE_MODE_HEAT)
    new_action = climate::CLIMATE_ACTION_OFF;
  else if (want_heat)
    new_action = climate::CLIMATE_ACTION_HEATING;
  else
    new_action = climate::CLIMATE_ACTION_IDLE;
  if (new_action != this->action) {
    this->action = new_action;
    changed = true;
  }
  return changed;
}

void PentairBody::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  this->update_();
  this->publish_state();
}

climate::ClimateTraits PentairBody::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  uint32_t features = climate::CLIMATE_SUPPORTS_ACTION;
  if (this->temp_sensor_ != nullptr)
    features |= climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE;
  traits.add_feature_flags(features);
  traits.set_visual_min_temperature(this->visual_min_);
  traits.set_visual_max_temperature(this->visual_max_);
  traits.set_visual_temperature_step(0.5f);
  return traits;
}

void PentairBody::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair Body thermostat '%s':", this->get_name().c_str());
  ESP_LOGCONFIG(TAG, "  Hysteresis: %.1f", this->hysteresis_);
  ESP_LOGCONFIG(TAG, "  Heat control: drives a heat-source (fireman) relay circuit");
  ESP_LOGCONFIG(TAG, "  Water temperature: from a local ESPHome sensor (not RS-485)");
  if (this->body_circuit_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Body-circuit interlock: enabled");
}

}  // namespace pentair
}  // namespace esphome
