#pragma once
#include "esphome/components/select/select.h"
#include "pentair_heater.h"

namespace esphome {
namespace pentair {

// Heat-pump mode select (Ultra + Hybrid). The three options are the firmware
// mode enum (Off / Heating / Cooling); picking one latches the command that the
// hub packs into the heater's next poll frame. Attaching this to a subtype
// without a proven bus command (the gas heaters) makes it a no-op
// (set_heat_pump_mode ignores unsupported subtypes).
class PentairHeaterModeSelect: public select::Select {
 public:
  void set_heater(PentairHeater *heater) { this->heater_ = heater; }

 protected:
  void control(const std::string &value) override {
    auto idx = this->index_of(value);
    if (idx.has_value() && this->heater_ != nullptr)
      this->heater_->set_heat_pump_mode(static_cast<HeatPumpMode>(*idx));
    this->publish_state(value);
  }
  PentairHeater *heater_{nullptr};
};

// Hybrid heat-source select (token 0xa5d6). Options are ordered to match the
// firmware enum offset by one (index 0 -> 1 "Heat Pump Only"... 3 -> 4 "Dual
// Mode"); it sets payload[1] of the Hybrid command. Meaningful only on a Hybrid.
class PentairHeaterHeatingModeSelect: public select::Select {
 public:
  void set_heater(PentairHeater *heater) { this->heater_ = heater; }

 protected:
  void control(const std::string &value) override {
    auto idx = this->index_of(value);
    if (idx.has_value() && this->heater_ != nullptr)
      this->heater_->set_hybrid_heating_mode(static_cast<HybridHeatingMode>(*idx + 1));
    this->publish_state(value);
  }
  PentairHeater *heater_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
