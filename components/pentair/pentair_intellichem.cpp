#include "pentair_intellichem.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.intellichem";

void PentairIntelliChem::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair IntelliChem:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", this->poll_interval_ms());
}

void PentairIntelliChem::on_status(const std::vector<uint8_t> &data) {
  this->mark_comm(true);  // a reply means IntelliChem is present on the bus

  // Decode the firmware-proven field layout (protocol.h).
  ChemStatus st;
  if (decode_chem_status(data.data(), data.size(), st)) {
    if (this->ph_sensor_ != nullptr)
      this->ph_sensor_->publish_state(st.ph);
    if (this->orp_sensor_ != nullptr)
      this->orp_sensor_->publish_state(st.orp);
    if (this->ph_setpoint_sensor_ != nullptr)
      this->ph_setpoint_sensor_->publish_state(st.ph_setpoint);
    if (this->orp_setpoint_sensor_ != nullptr)
      this->orp_setpoint_sensor_->publish_state(st.orp_setpoint);
    if (this->ph_tank_sensor_ != nullptr)
      this->ph_tank_sensor_->publish_state(st.ph_tank);
    if (this->orp_tank_sensor_ != nullptr)
      this->orp_tank_sensor_->publish_state(st.orp_tank);
    if (this->saturation_index_sensor_ != nullptr)
      this->saturation_index_sensor_->publish_state(st.saturation_index);
    if (this->calcium_hardness_sensor_ != nullptr)
      this->calcium_hardness_sensor_->publish_state(st.calcium_hardness);
    if (this->cyanuric_acid_sensor_ != nullptr)
      this->cyanuric_acid_sensor_->publish_state(st.cyanuric_acid);
    if (this->total_alkalinity_sensor_ != nullptr)
      this->total_alkalinity_sensor_->publish_state(st.total_alkalinity);

    // Cache the settable config so a setpoint write can resend the untouched
    // fields with their current values (firmware sends all seven together).
    this->cfg_.ph_setpoint_x100 = static_cast<uint16_t>(lroundf(st.ph_setpoint * 100.0f));
    this->cfg_.orp_setpoint = st.orp_setpoint;
    this->cfg_.ph_tank = st.ph_tank;
    this->cfg_.orp_tank = st.orp_tank;
    this->cfg_.calcium_hardness = st.calcium_hardness;
    this->cfg_.cyanuric_acid = st.cyanuric_acid;
    this->cfg_.total_alkalinity = st.total_alkalinity;
    this->have_config_ = true;

    // Named alarm / warning / dosing state (data[32..35]). Only present
    // when the reply is long enough (st.has_alarms); otherwise leave untouched.
    if (st.has_alarms) {
      if (this->alarms_ts_ != nullptr)
        this->alarms_ts_->publish_state(chem_alarm_string(st.alarms));
      if (this->warnings_ts_ != nullptr)
        this->warnings_ts_->publish_state(chem_warning_string(st.warnings));
      if (this->ph_dosing_ts_ != nullptr)
        this->ph_dosing_ts_->publish_state(chem_dosing_status_name(st.ph_dosing));
      if (this->orp_dosing_ts_ != nullptr)
        this->orp_dosing_ts_->publish_state(chem_dosing_status_name(st.orp_dosing));
    }
  }

  // Also surface the raw reply payload as hex for diagnostics.
  if (this->reply_ts_ != nullptr) {
    std::string hex;
    hex.reserve(data.size() * 3);
    char b[4];
    for (uint8_t v: data) {
      std::snprintf(b, sizeof(b), "%02X ", v);
      hex += b;
    }
    if (!hex.empty())
      hex.pop_back();
    this->reply_ts_->publish_state(hex);
  }
}

void PentairIntelliChem::mark_comm(bool ok) {
  this->comm_ok_ = ok;
  if (this->comm_bs_ != nullptr)
    this->comm_bs_->publish_state(ok);
}

void PentairIntelliChem::begin_edit_() {
  // Seed the pending copy from the current device config only when starting a
  // fresh edit, so successive setter calls accumulate onto the same draft.
  if (!this->write_pending_)
    this->pending_ = this->cfg_;
}

void PentairIntelliChem::set_ph_setpoint(float ph) {
  if (!this->have_config_) {
    ESP_LOGW(TAG, "IntelliChem @ 0x%02X: no status yet, ignoring pH setpoint write", this->address_);
    return;
  }
  this->begin_edit_();
  this->pending_.ph_setpoint_x100 = static_cast<uint16_t>(lroundf(ph * 100.0f));
  this->write_pending_ = true;
}

void PentairIntelliChem::set_orp_setpoint(float mv) {
  if (!this->have_config_) {
    ESP_LOGW(TAG, "IntelliChem @ 0x%02X: no status yet, ignoring ORP setpoint write", this->address_);
    return;
  }
  this->begin_edit_();
  this->pending_.orp_setpoint = static_cast<uint16_t>(lroundf(mv));
  this->write_pending_ = true;
}

void PentairIntelliChem::set_calcium_hardness(float ppm) {
  if (!this->have_config_) {
    ESP_LOGW(TAG, "IntelliChem @ 0x%02X: no status yet, ignoring hardness write", this->address_);
    return;
  }
  this->begin_edit_();
  this->pending_.calcium_hardness = static_cast<uint16_t>(lroundf(ppm));
  this->write_pending_ = true;
}

void PentairIntelliChem::set_cyanuric_acid(float ppm) {
  if (!this->have_config_) {
    ESP_LOGW(TAG, "IntelliChem @ 0x%02X: no status yet, ignoring CYA write", this->address_);
    return;
  }
  this->begin_edit_();
  this->pending_.cyanuric_acid = static_cast<uint16_t>(lroundf(ppm));
  this->write_pending_ = true;
}

void PentairIntelliChem::set_total_alkalinity(float ppm) {
  if (!this->have_config_) {
    ESP_LOGW(TAG, "IntelliChem @ 0x%02X: no status yet, ignoring alkalinity write", this->address_);
    return;
  }
  this->begin_edit_();
  this->pending_.total_alkalinity = static_cast<uint16_t>(lroundf(ppm));
  this->write_pending_ = true;
}

bool PentairIntelliChem::take_pending_write(uint8_t *payload) {
  if (!this->write_pending_ || !this->have_config_)
    return false;
  build_chem_set_payload(this->pending_, payload);
  this->write_pending_ = false;
  return true;
}

}  // namespace pentair
}  // namespace esphome
