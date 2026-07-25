#pragma once
#include "pentair_pump.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace pentair {

// Pump run/stop switch.
class PentairPumpSwitch : public switch_::Switch {
 public:
  void set_pump(PentairPump *pump) { this->pump_ = pump; }

 protected:
  void write_state(bool state) override {
    this->pump_->set_run(state);
    this->publish_state(state);
  }
  PentairPump *pump_{nullptr};
};

}  // namespace pentair
}  // namespace esphome
