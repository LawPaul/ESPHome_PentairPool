#include "pentair_heater.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <string>

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.heater";

static const char *heater_type_name(HeaterType t) {
  switch (t) {
    case HEATER_TYPE_ULTRA:
      return "UltraTemp";
    case HEATER_TYPE_HYBRID:
      return "Hybrid";
    case HEATER_TYPE_MASTERTEMP:
      return "MasterTemp";
    case HEATER_TYPE_MAXETHERM:
      return "MaxE-Therm";
    case HEATER_TYPE_ETI250:
      return "ETI250";
    default:
      return "Heater";
  }
}

bool PentairHeater::matches_reply_cmd(uint8_t cmd) const {
  switch (this->type_) {
    case HEATER_TYPE_ULTRA:
      return cmd == 0x73;
    case HEATER_TYPE_HYBRID:
      return cmd == 0x71;
    case HEATER_TYPE_ETI250:
      return cmd == 0x81;
    default:  // MasterTemp / MaxE-Therm both reply 0x74 (disambiguated by src)
      return cmd == 0x74;
  }
}

void PentairHeater::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair Heater (%s):", heater_type_name(this->type_));
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", this->poll_interval_ms());
}

void PentairHeater::on_status(const std::vector<uint8_t> &data) {
  this->mark_comm(true);  // a reply means the heater is present on the bus

  // Field offsets within the A5 data payload. The firmware parser proves the
  // field order/spacing; its packet-buffer base (+0x8b) maps to data[0], which
  // is cross-checked against the IntelliChem parser. See protocol.h.
  auto pub = [&](sensor::Sensor *s, uint8_t off) {
    if (s != nullptr && data.size() > off)
      s->publish_state(static_cast<float>(data[off]));
  };
  pub(this->mode_sensor_, HEATER_OFF_MODE);
  pub(this->status_sensor_, HEATER_OFF_STATUS);
  pub(this->error_a_sensor_, HEATER_OFF_ERR_A);
  pub(this->error_b_sensor_, HEATER_OFF_ERR_B);
  pub(this->fenwal_sensor_, HEATER_OFF_FENWAL);

  const bool is_hp = heater_is_heat_pump(this->type_);

  // Fault bytes differ by heater family (firmware-proven):
  //  - Gas (MaxE-Therm/ETI250): ErrorFlagsA=data[3], ErrorFlagsB=data[4],
  //    FenwalDiag=data[13].
  //  - Heat pump (Ultra/Hybrid): alarm bits in data[7]/data[8]/data[9].
  const uint8_t err_a = data.size() > HEATER_OFF_ERR_A ? data[HEATER_OFF_ERR_A]: 0;
  const uint8_t err_b = data.size() > HEATER_OFF_ERR_B ? data[HEATER_OFF_ERR_B]: 0;
  const uint8_t fenwal = data.size() > HEATER_OFF_FENWAL ? data[HEATER_OFF_FENWAL]: 0;
  const uint8_t hp_d7 = data.size() > HEATER_OFF_HP_D7 ? data[HEATER_OFF_HP_D7]: 0;
  const uint8_t hp_d8 = data.size() > HEATER_OFF_HP_D8 ? data[HEATER_OFF_HP_D8]: 0;
  const uint8_t hp_d9 = data.size() > HEATER_OFF_HP_D9 ? data[HEATER_OFF_HP_D9]: 0;

  // Aggregate fault flag: any alarm bit set (heat pump) or any error byte / a
  // non-zero Fenwal ignition fault (gas).
  const bool fault = is_hp ? (hp_d7 != 0 || hp_d8 != 0 || hp_d9 != 0)
                           : (err_a != 0 || err_b != 0 || (fenwal & 0x0f) != 0);
  if (this->fault_bs_ != nullptr)
    this->fault_bs_->publish_state(fault);

  // Named fault reason. All four supported subtypes decode to firmware labels:
  // the Fenwal gas heaters via heater_fault_string(), the Ultra/Hybrid
  // heat pumps via heat_pump_fault_string().
  if (this->fault_ts_ != nullptr) {
    std::string reason = is_hp ? heat_pump_fault_string(this->type_, hp_d7, hp_d8, hp_d9)
                               : heater_fault_string(this->type_, err_a, err_b, fenwal);
    if (reason.empty())
      reason = fault ? "Fault": "OK";
    this->fault_ts_->publish_state(reason);
  }

  if (this->status_ts_ != nullptr && data.size() > HEATER_OFF_STATUS) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "mode %u status %u%s", (unsigned) data[HEATER_OFF_MODE],
                  (unsigned) data[HEATER_OFF_STATUS], fault ? " fault": "");
    this->status_ts_->publish_state(buf);
  }
}

void PentairHeater::mark_comm(bool ok) {
  this->comm_ok_ = ok;
  if (this->comm_bs_ != nullptr)
    this->comm_bs_->publish_state(ok);
}

}  // namespace pentair
}  // namespace esphome
