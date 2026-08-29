#include "pentair_pump.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.pump";

void PentairPump::dump_config() {
  const char *type_str = this->type_ == PUMP_TYPE_VS    ? "VS"
                         : this->type_ == PUMP_TYPE_VSF ? "VSF"
                                                        : "VF";
  ESP_LOGCONFIG(TAG, "Pentair Pump:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  Type: %s", type_str);
  if (this->type_ == PUMP_TYPE_VSF)
    ESP_LOGCONFIG(TAG, "  Mode: %s", this->mode_ == PUMP_MODE_SPEED ? "speed": "flow");
}

uint16_t PentairPump::clamp_setpoint_() const {
  return clamp_setpoint(this->type_, this->mode_, this->target_, this->min_flow_);
}

void PentairPump::build_setpoint(uint8_t &cmd, std::vector<uint8_t> &data) const {
  encode_setpoint(this->type_, this->mode_, this->target_, cmd, data, this->min_flow_);
}

void PentairPump::on_status(const PumpStatus &st) {
  this->running_ = st.state != PUMP_STATE_STOPPED;
  if (this->rpm_sensor_ != nullptr)
    this->rpm_sensor_->publish_state(st.rpm);
  if (this->gpm_sensor_ != nullptr)
    this->gpm_sensor_->publish_state(st.gpm);
  if (this->watts_sensor_ != nullptr)
    this->watts_sensor_->publish_state(st.watts);
  if (this->priming_bs_ != nullptr)
    this->priming_bs_->publish_state(st.state == PUMP_STATE_PRIMING);
  if (this->alarm_bs_ != nullptr)
    this->alarm_bs_->publish_state(st.alarms != 0);
  // Per-bit fault breakout (names verbatim from IntelliFloVSF_logStatusPacket).
  if (this->high_temp_bs_ != nullptr)
    this->high_temp_bs_->publish_state((st.alarms & PUMP_ALARM_HIGH_TEMP) != 0);
  if (this->prime_error_bs_ != nullptr)
    this->prime_error_bs_->publish_state((st.alarms & PUMP_ALARM_PRIME) != 0);
  if (this->over_temp_bs_ != nullptr)
    this->over_temp_bs_->publish_state((st.alarms & PUMP_ALARM_OVER_TEMP) != 0);
  if (this->power_error_bs_ != nullptr)
    this->power_error_bs_->publish_state((st.alarms & PUMP_ALARM_POWER) != 0);
  if (this->over_current_bs_ != nullptr)
    this->over_current_bs_->publish_state((st.alarms & PUMP_ALARM_OVER_CURRENT) != 0);
  if (this->over_voltage_bs_ != nullptr)
    this->over_voltage_bs_->publish_state((st.alarms & PUMP_ALARM_OVER_VOLTAGE) != 0);
  if (this->unknown_alarm_bs_ != nullptr)
    this->unknown_alarm_bs_->publish_state((st.alarms & PUMP_ALARM_UNKNOWN) != 0);
  this->mark_comm(true);
}

void PentairPump::mark_comm(bool ok) {
  this->comm_ok_ = ok;
  if (this->comm_bs_ != nullptr)
    this->comm_bs_->publish_state(ok);
  if (!ok) {
    // Link went stale. NOTE: because the pump (and the cell) are powered from
    // the pump relay's load side, "no comm" is the NORMAL resting state whenever
    // the pump is intentionally off -- so we must NOT fabricate specific faults
    // here or every routine pump-off would flood false alarms. Instead:
    //  - running_ -> false ("assume not running"): the one SAFE-direction change
    //    -- cuts the booster dry-run guard and flow-gates the cell to 0%.
    //  - numeric telemetry -> NaN so Home Assistant shows "unknown", not a stale
    //    RPM / flow / watts.
    //  - every alarm/problem flag -> its NORMAL (non-problem) state.
    // The comm-lost condition itself is annunciated by the "Connected" sensor +
    // the powered-but-silent "Comms Lost" watchdog, which is the correct signal.
    this->running_ = false;
    if (this->rpm_sensor_ != nullptr)
      this->rpm_sensor_->publish_state(NAN);
    if (this->gpm_sensor_ != nullptr)
      this->gpm_sensor_->publish_state(NAN);
    if (this->watts_sensor_ != nullptr)
      this->watts_sensor_->publish_state(NAN);
    if (this->priming_bs_ != nullptr)
      this->priming_bs_->publish_state(false);
    if (this->alarm_bs_ != nullptr)
      this->alarm_bs_->publish_state(false);
    if (this->high_temp_bs_ != nullptr)
      this->high_temp_bs_->publish_state(false);
    if (this->prime_error_bs_ != nullptr)
      this->prime_error_bs_->publish_state(false);
    if (this->over_temp_bs_ != nullptr)
      this->over_temp_bs_->publish_state(false);
    if (this->power_error_bs_ != nullptr)
      this->power_error_bs_->publish_state(false);
    if (this->over_current_bs_ != nullptr)
      this->over_current_bs_->publish_state(false);
    if (this->over_voltage_bs_ != nullptr)
      this->over_voltage_bs_->publish_state(false);
    if (this->unknown_alarm_bs_ != nullptr)
      this->unknown_alarm_bs_->publish_state(false);
  }
}

}  // namespace pentair
}  // namespace esphome
