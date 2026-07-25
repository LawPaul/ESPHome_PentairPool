#include "pentair_chlorinator.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.chlorinator";

// IntelliChlor salt reading and alarm bit masks live in protocol.h (CHLOR_*)
// so the host-side test suite can validate them against real captures.

void PentairChlorinator::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair IntelliChlor:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
}

void PentairChlorinator::on_status(uint8_t salt_raw, uint8_t alarms) {
  this->mark_comm(true);  // a reply means the cell is present on the bus
  if (this->salt_sensor_ != nullptr)
    this->salt_sensor_->publish_state(static_cast<float>(salt_raw) * CHLOR_SALT_SCALE);
  if (this->no_flow_bs_ != nullptr)
    this->no_flow_bs_->publish_state((alarms & CHLOR_ALARM_NO_FLOW) != 0);
  if (this->low_salt_bs_ != nullptr)
    this->low_salt_bs_->publish_state((alarms & CHLOR_ALARM_LOW_SALT) != 0);
  if (this->very_low_salt_bs_ != nullptr)
    this->very_low_salt_bs_->publish_state((alarms & CHLOR_ALARM_VERY_LOW_SALT) != 0);
  if (this->clean_cell_bs_ != nullptr)
    this->clean_cell_bs_->publish_state((alarms & CHLOR_ALARM_CLEAN_CELL) != 0);
  if (this->cold_water_bs_ != nullptr)
    this->cold_water_bs_->publish_state((alarms & CHLOR_ALARM_COLD_WATER) != 0);
}

void PentairChlorinator::on_flow_gate(bool present) {
  if (this->flow_bs_ != nullptr)
    this->flow_bs_->publish_state(present);
}

void PentairChlorinator::mark_comm(bool ok) {
  this->comm_ok_ = ok;
  if (this->comm_bs_ != nullptr)
    this->comm_bs_->publish_state(ok);
  if (!ok) {
    // Link went stale. As with the pump, "no comm" is the NORMAL resting state
    // whenever the cell is unpowered (its power follows the pump relay), so we
    // clear rather than fabricate faults: blank the salt reading to "unknown"
    // (NaN), drop the positive flow indication, and set every alarm/problem flag
    // to its NORMAL (non-problem) state. The comm-lost condition is annunciated
    // by the "Connected" sensor + the "Comms Lost" watchdog.
    if (this->salt_sensor_ != nullptr)
      this->salt_sensor_->publish_state(NAN);
    if (this->flow_bs_ != nullptr)
      this->flow_bs_->publish_state(false);
    if (this->no_flow_bs_ != nullptr)
      this->no_flow_bs_->publish_state(false);
    if (this->low_salt_bs_ != nullptr)
      this->low_salt_bs_->publish_state(false);
    if (this->very_low_salt_bs_ != nullptr)
      this->very_low_salt_bs_->publish_state(false);
    if (this->clean_cell_bs_ != nullptr)
      this->clean_cell_bs_->publish_state(false);
    if (this->cold_water_bs_ != nullptr)
      this->cold_water_bs_->publish_state(false);
  }
}

}  // namespace pentair
}  // namespace esphome
