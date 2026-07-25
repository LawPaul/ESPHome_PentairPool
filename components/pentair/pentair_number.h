#pragma once
#include "pentair_pump.h"
#include "pentair_chlorinator.h"
#include "pentair_intellichem.h"
#include "pentair_heater.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace pentair {

// Pump speed/flow setpoint number.
class PentairPumpNumber: public number::Number {
 public:
  void set_pump(PentairPump *pump) { this->pump_ = pump; }

 protected:
  void control(float value) override {
    this->pump_->set_target(static_cast<uint16_t>(value));
    this->publish_state(value);
  }
  PentairPump *pump_{nullptr};
};

// IntelliChlor output-percent number.
class PentairChlorNumber: public number::Number {
 public:
  void set_chlorinator(PentairChlorinator *chlor) { this->chlor_ = chlor; }

 protected:
  void control(float value) override {
    this->chlor_->set_output(static_cast<uint8_t>(value));
    this->publish_state(value);
  }
  PentairChlorinator *chlor_{nullptr};
};

// IntelliChem setpoint / LSI-input write number. The field selector routes the
// value to the matching 0x92 config-write setter (firmware binder FUN_0096f3bc).
enum class ChemNumberField {
  PH_SETPOINT,
  ORP_SETPOINT,
  CALCIUM_HARDNESS,
  CYANURIC_ACID,
  TOTAL_ALKALINITY,
};

class PentairChemNumber: public number::Number {
 public:
  void set_intellichem(PentairIntelliChem *chem) { this->chem_ = chem; }
  void set_field(ChemNumberField field) { this->field_ = field; }

 protected:
  void control(float value) override {
    switch (this->field_) {
      case ChemNumberField::PH_SETPOINT:
        this->chem_->set_ph_setpoint(value);
        break;
      case ChemNumberField::ORP_SETPOINT:
        this->chem_->set_orp_setpoint(value);
        break;
      case ChemNumberField::CALCIUM_HARDNESS:
        this->chem_->set_calcium_hardness(value);
        break;
      case ChemNumberField::CYANURIC_ACID:
        this->chem_->set_cyanuric_acid(value);
        break;
      case ChemNumberField::TOTAL_ALKALINITY:
        this->chem_->set_total_alkalinity(value);
        break;
    }
    this->publish_state(value);
  }
  PentairIntelliChem *chem_{nullptr};
  ChemNumberField field_{ChemNumberField::PH_SETPOINT};
};

// Hybrid heat-pump command fields (FUN_0097b558). SET_POINT is the target
// water temp (payload[2]); BOOST is boostTemp (payload[4]); PARAM is the
// payload[3]/0xf38c byte (the heater setter clamps it to [5,60]).
enum class HeaterNumberField {
  HYBRID_SET_POINT,
  HYBRID_BOOST,
  HYBRID_PARAM,
};

class PentairHeaterNumber: public number::Number {
 public:
  void set_heater(PentairHeater *heater) { this->heater_ = heater; }
  void set_field(HeaterNumberField field) { this->field_ = field; }

 protected:
  void control(float value) override {
    if (this->heater_ != nullptr) {
      switch (this->field_) {
        case HeaterNumberField::HYBRID_SET_POINT:
          this->heater_->set_hybrid_set_point(static_cast<uint8_t>(value));
          break;
        case HeaterNumberField::HYBRID_BOOST:
          this->heater_->set_hybrid_boost_temp(static_cast<uint8_t>(value));
          break;
        case HeaterNumberField::HYBRID_PARAM:
          this->heater_->set_hybrid_param(static_cast<uint8_t>(value));
          break;
      }
    }
    this->publish_state(value);
  }
  PentairHeater *heater_{nullptr};
  HeaterNumberField field_{HeaterNumberField::HYBRID_SET_POINT};
};

}  // namespace pentair
}  // namespace esphome
