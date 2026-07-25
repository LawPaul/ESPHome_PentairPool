#include "pentair_hub.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace pentair {

static const char *const TAG = "pentair.hub";

void PentairHub::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);  // receive by default
  }
}

void PentairHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Pentair Hub:");
  ESP_LOGCONFIG(TAG, "  Source address: 0x%02X", this->source_address_);
  ESP_LOGCONFIG(TAG, "  Poll interval (idle): %u ms", this->poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Poll interval (active): %u ms", this->active_poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Require pump flow for chlorination: %s", YESNO(this->require_pump_flow_));
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
  // Device routing table: the configured devices we poll and expect to answer.
  // Presence is discovered at runtime from whether each address replies
  // (Connected/Disconnected), just like the OCP's config-driven enumeration.
  ESP_LOGCONFIG(TAG, "  Device routing table (%u configured):",
                (unsigned) (this->pumps_.size() + this->chlorinators_.size() +
                            this->heaters_.size() + this->intellichems_.size()));
  for (auto *pump: this->pumps_)
    ESP_LOGCONFIG(TAG, "    Pump        @ 0x%02X", pump->address());
  for (auto *chlor: this->chlorinators_)
    ESP_LOGCONFIG(TAG, "    Chlorinator @ 0x%02X", chlor->address());
  for (auto *heater: this->heaters_)
    ESP_LOGCONFIG(TAG, "    Heater      @ 0x%02X (poll %u ms)", heater->address(),
                  heater->poll_interval_ms());
  for (auto *chem: this->intellichems_)
    ESP_LOGCONFIG(TAG, "    IntelliChem @ 0x%02X (poll %u ms)", chem->address(),
                  chem->poll_interval_ms());
}

void PentairHub::loop() {
  this->read_uart_();
  this->parse_buffer_();

  const uint32_t now = millis();

  // Comm timeout: if a pump hasn't answered for 3 idle poll cycles, flag it down.
  for (size_t i = 0; i < this->pumps_.size(); i++) {
    if (this->pump_last_status_ms_[i] != 0 && this->pumps_[i]->comm_ok() &&
        (now - this->pump_last_status_ms_[i]) > (3 * this->poll_interval_ms_)) {
      this->pumps_[i]->mark_comm(false);
    }
  }

  // Same presence logic for the chlorinator(s): connected on reply, marked
  // disconnected after 3 idle cycles of silence (firmware Connected/Disconnected).
  for (size_t i = 0; i < this->chlorinators_.size(); i++) {
    if (this->chlor_last_status_ms_[i] != 0 && this->chlorinators_[i]->comm_ok() &&
        (now - this->chlor_last_status_ms_[i]) > (3 * this->poll_interval_ms_)) {
      this->chlorinators_[i]->mark_comm(false);
    }
  }

  // Slow devices (heaters, IntelliChem) use their own firmware cadence, so the
  // silence threshold is scaled to each device's poll interval.
  for (size_t i = 0; i < this->heaters_.size(); i++) {
    if (this->heater_last_status_ms_[i] != 0 && this->heaters_[i]->comm_ok() &&
        (now - this->heater_last_status_ms_[i]) > (3 * this->heaters_[i]->poll_interval_ms())) {
      this->heaters_[i]->mark_comm(false);
    }
  }
  for (size_t i = 0; i < this->intellichems_.size(); i++) {
    if (this->chem_last_status_ms_[i] != 0 && this->intellichems_[i]->comm_ok() &&
        (now - this->chem_last_status_ms_[i]) > (3 * this->intellichems_[i]->poll_interval_ms())) {
      this->intellichems_[i]->mark_comm(false);
    }
  }

  // Dynamic cadence, mirroring the OCP: any active pump pulls the whole cycle
  // down to the fast (2 s) interval; otherwise poll at the idle (16 s) rate.
  bool any_active = false;
  for (auto *pump: this->pumps_) {
    if (pump->is_active()) {
      any_active = true;
      break;
    }
  }
  const uint32_t interval = any_active ? this->active_poll_interval_ms_: this->poll_interval_ms_;

  // Refill the queue once it drains and the (state-dependent) interval elapses.
  if (this->tx_queue_.empty() && (now - this->last_poll_ms_) >= interval) {
    this->last_poll_ms_ = now;
    for (auto *pump: this->pumps_)
      this->queue_pump_poll_(pump);
    for (auto *chlor: this->chlorinators_)
      this->queue_chlor_command_(chlor);
  }

  // Slow devices are scheduled independently, each on its own firmware cadence
  // (heaters 30/50 s, IntelliChem 30 s), and only enqueued while the bus is
  // idle so they never contend with an in-flight pump/chlorinator batch.
  if (this->tx_queue_.empty()) {
    // IntelliChem setpoint writes are latched by the user and take priority
    // over the next poll so a changed setpoint reaches the device promptly.
    for (auto *chem: this->intellichems_)
      this->queue_chem_set_(chem);
  }
  if (this->tx_queue_.empty()) {
    for (size_t i = 0; i < this->heaters_.size(); i++) {
      if ((now - this->heater_last_poll_ms_[i]) >= this->heaters_[i]->poll_interval_ms()) {
        this->heater_last_poll_ms_[i] = now;
        this->queue_heater_poll_(this->heaters_[i]);
      }
    }
    for (size_t i = 0; i < this->intellichems_.size(); i++) {
      if ((now - this->chem_last_poll_ms_[i]) >= this->intellichems_[i]->poll_interval_ms()) {
        this->chem_last_poll_ms_[i] = now;
        this->queue_chem_poll_(this->intellichems_[i]);
      }
    }
  }

  // Space frames out so devices can answer on the half-duplex bus.
  if (!this->tx_queue_.empty() && (now - this->last_tx_ms_) >= this->tx_gap_ms_) {
    this->send_next_frame_();
    this->last_tx_ms_ = now;
  }
}

// ---------------------------------------------------------------------------
// TX
// ---------------------------------------------------------------------------

void PentairHub::enqueue_frame_(const std::vector<uint8_t> &frame) { this->tx_queue_.push_back(frame); }

void PentairHub::send_next_frame_() {
  if (this->tx_queue_.empty())
    return;
  const std::vector<uint8_t> &frame = this->tx_queue_.front();

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);  // drive the bus

  this->write_array(frame.data(), frame.size());
  this->flush();  // block until the UART FIFO is fully shifted out

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);  // release to receive

  this->tx_queue_.erase(this->tx_queue_.begin());
}

void PentairHub::queue_pump_poll_(PentairPump *pump) {
  // Idle pump: the OCP only polls status. Emit the status request and stop.
  if (!pump->is_active()) {
    this->queue_pump_status_(pump);
    return;
  }

  std::vector<uint8_t> frame;
  uint8_t dst = pump->address();

  // 1) HandOn (cmd 4 = 0xFF): hold the pump under remote control. The OCP
  //    re-asserts this as the first frame of every active batch; the pump
  //    reverts to its local program if the control packet stops arriving, so
  //    the fast (2 s) active cadence is what keeps it in remote.
  uint8_t remote = PUMP_REMOTE_ON;
  build_a5_frame(frame, dst, this->source_address_, PUMP_CMD_REMOTE, &remote, 1);
  this->enqueue_frame_(frame);

  // 2) Setpoint write (register/prefix depends on pump type).
  uint8_t sp_cmd;
  std::vector<uint8_t> sp_data;
  pump->build_setpoint(sp_cmd, sp_data);
  build_a5_frame(frame, dst, this->source_address_, sp_cmd, sp_data.data(), sp_data.size());
  this->enqueue_frame_(frame);

  // 2a) VF feature/menu select (cmd 5, payload 0x06). The OCP sends this only
  //     to VF pumps as part of the drive sequence (firmware VF setupMessages
  //     registers cmd 5; binder FUN_0096baf0 writes the fixed byte 0x06),
  //     confirmed against njspc's setPumpFeature(6). Sent when driving the
  //     pump, matching njspc's targetSpeed>0 gate.
  if (pump->pump_type() == PUMP_TYPE_VF && pump->run_requested()) {
    uint8_t feature = PUMP_FEATURE_1;
    build_a5_frame(frame, dst, this->source_address_, PUMP_CMD_FEATURE, &feature, 1);
    this->enqueue_frame_(frame);
  }

  // 3) Run / stop.
  uint8_t run = pump->run_requested() ? PUMP_RUN: PUMP_STOP;
  build_a5_frame(frame, dst, this->source_address_, PUMP_CMD_RUN, &run, 1);
  this->enqueue_frame_(frame);

  // 4) Status request (reply parsed in dispatch_).
  build_a5_frame(frame, dst, this->source_address_, PUMP_CMD_STATUS, nullptr, 0);
  this->enqueue_frame_(frame);

  // The change has now been emitted; clear the pending flag so the cadence
  // can relax back to idle once the pump stops.
  pump->clear_pending();
}

void PentairHub::queue_pump_status_(PentairPump *pump) {
  std::vector<uint8_t> frame;
  build_a5_frame(frame, pump->address(), this->source_address_, PUMP_CMD_STATUS, nullptr, 0);
  this->enqueue_frame_(frame);
}

void PentairHub::queue_chlor_command_(PentairChlorinator *chlor) {
  const bool flow = this->flow_present_();
  chlor->on_flow_gate(flow);

  // Mirror the firmware: output is gated by a boolean "flow present" signal,
  // not a numeric RPM/GPM threshold. No flow -> command 0%.
  uint8_t out = flow ? chlor->requested_output(): 0;

  std::vector<uint8_t> frame;
  build_chlor_frame(frame, chlor->address(), CHLOR_CMD_SET_OUTPUT, &out, 1);
  this->enqueue_frame_(frame);
}

void PentairHub::queue_heater_poll_(PentairHeater *heater) {
  // Heater status request. For GAS heaters the request payload is NOT decoded
  // in firmware, so it is sent zero-filled at the firmware-declared
  // length. For the ULTRA heat pump the poll frame doubles as a command: once
  // the user has commanded a mode, fill_request_payload() packs the
  // 0x90/mode/service bytes (setUltraParams); otherwise it too stays
  // zero-filled. (The Hybrid uses a different, not-yet-reversed layout and is
  // left telemetry-only.) Either way the frame shape is faithful and the reply
  // carries the telemetry we parse.
  const uint8_t len = heater->request_len();
  std::vector<uint8_t> data(len, 0);
  heater->fill_request_payload(data.data(), len);
  std::vector<uint8_t> frame;
  build_a5_frame(frame, heater->address(), this->source_address_, heater->request_cmd(), data.data(),
                 len);
  this->enqueue_frame_(frame);
}

void PentairHub::queue_chem_poll_(PentairIntelliChem *chem) {
  // IntelliChem status poll: cmd 0xd2 with a single payload byte 0xD2 (the
  // firmware binder FUN_0096f3b0 writes exactly this) -> reply 0x12.
  uint8_t data = CHEM_CMD_STATUS;
  std::vector<uint8_t> frame;
  build_a5_frame(frame, chem->address(), this->source_address_, CHEM_CMD_STATUS, &data, 1);
  this->enqueue_frame_(frame);
}

void PentairHub::queue_chem_set_(PentairIntelliChem *chem) {
  // IntelliChem setpoint write: cmd 0x92 with the 21-byte config payload
  // (firmware binder FUN_0096f3bc). Only emitted when the user has changed a
  // setpoint AND a status reply has been decoded, so untouched fields carry
  // their current values. The device answers with an ACK (cmd 1).
  uint8_t data[CHEM_SET_PAYLOAD_LEN];
  if (!chem->take_pending_write(data))
    return;
  std::vector<uint8_t> frame;
  build_a5_frame(frame, chem->address(), this->source_address_, CHEM_CMD_SET, data,
                 CHEM_SET_PAYLOAD_LEN);
  this->enqueue_frame_(frame);
}

bool PentairHub::flow_present_() {
  if (!this->require_pump_flow_)
    return true;
  if (this->pumps_.empty())
    return true;  // no Pentair pump to gate on; assume external flow
  for (auto *pump: this->pumps_) {
    if (pump->running())
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// RX
// ---------------------------------------------------------------------------

void PentairHub::read_uart_() {
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    this->rx_buffer_.push_back(b);
    this->last_rx_byte_ms_ = millis();
  }
}

void PentairHub::parse_buffer_() {
  bool progress = true;
  while (progress) {
    progress = false;
    const size_t n = this->rx_buffer_.size();
    for (size_t i = 0; i < n; i++) {
      // A5 frame: preceded by the FF 00 FF preamble.
      if (this->rx_buffer_[i] == A5_START && i >= 3 && this->rx_buffer_[i - 1] == A5_PREAMBLE_2 &&
          this->rx_buffer_[i - 2] == A5_PREAMBLE_1 && this->rx_buffer_[i - 3] == A5_PREAMBLE_0) {
        progress = this->try_parse_a5_(i);
        break;
      }
      // IntelliChlor frame.
      if (this->rx_buffer_[i] == CHLOR_DLE && i + 1 < n && this->rx_buffer_[i + 1] == CHLOR_STX) {
        progress = this->try_parse_chlor_(i);
        break;
      }
    }
    if (!progress) {
      // No complete frame yet; bound memory if we are accumulating junk.
      if (this->rx_buffer_.size() > 256)
        this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.end() - 64);
      break;
    }
  }
}

bool PentairHub::try_parse_a5_(size_t start) {
  // Layout from the A5 byte: A5 ver dst src cmd len data[len] ckHi ckLo
  if (this->rx_buffer_.size() < start + 6)
    return false;  // need the header
  const uint8_t len = this->rx_buffer_[start + 5];
  const size_t frame_end = start + 6 + len + 2;
  if (this->rx_buffer_.size() < frame_end)
    return false;  // wait for the rest

  const uint16_t calc = a5_checksum(&this->rx_buffer_[start], &this->rx_buffer_[start + 6 + len]);
  const uint16_t got =
      (uint16_t(this->rx_buffer_[start + 6 + len]) << 8) | this->rx_buffer_[start + 6 + len + 1];

  if (calc == got) {
    RxFrame f;
    f.is_chlor = false;
    f.dst = this->rx_buffer_[start + 2];
    f.src = this->rx_buffer_[start + 3];
    f.cmd = this->rx_buffer_[start + 4];
    f.data.assign(this->rx_buffer_.begin() + start + 6, this->rx_buffer_.begin() + start + 6 + len);
    this->dispatch_(f);
  } else {
    ESP_LOGW(TAG, "A5 checksum mismatch (calc=0x%04X got=0x%04X)", calc, got);
  }
  // Drop everything up to and including this frame (also clears the preamble).
  this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + frame_end);
  return true;
}

bool PentairHub::try_parse_chlor_(size_t start) {
  // 10 02 dst cmd data... ck 10 03
  const size_t n = this->rx_buffer_.size();
  for (size_t j = start + 2; j + 1 < n; j++) {
    if (this->rx_buffer_[j] == CHLOR_DLE && this->rx_buffer_[j + 1] == CHLOR_ETX) {
      const size_t frame_end = j + 2;
      if (j >= start + 4) {  // at least dst + cmd + checksum
        const uint8_t calc = chlor_checksum(&this->rx_buffer_[start], &this->rx_buffer_[j - 1]);
        const uint8_t got = this->rx_buffer_[j - 1];
        if (calc == got) {
          RxFrame f;
          f.is_chlor = true;
          f.dst = this->rx_buffer_[start + 2];
          f.src = 0;
          f.cmd = this->rx_buffer_[start + 3];
          f.data.assign(this->rx_buffer_.begin() + start + 4, this->rx_buffer_.begin() + j - 1);
          this->dispatch_(f);
        } else {
          ESP_LOGW(TAG, "IntelliChlor checksum mismatch (calc=0x%02X got=0x%02X)", calc, got);
        }
      }
      this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + frame_end);
      return true;
    }
  }
  return false;  // ETX not seen yet
}

void PentairHub::dispatch_(const RxFrame &frame) {
  if (frame.is_chlor) {
    if (frame.cmd == CHLOR_CMD_STATUS && frame.data.size() >= 2) {
      const uint32_t now = millis();
      for (size_t i = 0; i < this->chlorinators_.size(); i++) {
        this->chlorinators_[i]->on_status(frame.data[0], frame.data[1]);
        this->chlor_last_status_ms_[i] = now;
      }
    }
    return;
  }

  // A5 pump status reply (cmd 0x07); the pump answers with src = its address.
  if (frame.cmd == PUMP_CMD_STATUS) {
    for (size_t i = 0; i < this->pumps_.size(); i++) {
      if (this->pumps_[i]->address() != frame.src)
        continue;
      const auto &d = frame.data;
      // Decode run-state, telemetry and alarm word from the 0x07 reply
      // (firmware IntelliFloVSF_decodeStatus07 / logStatusPacket, protocol.h).
      PumpStatus st;
      decode_pump_status(d.data(), d.size(), st);
      this->pumps_[i]->on_status(st);
      this->pump_last_status_ms_[i] = millis();
      break;
    }
    return;
  }

  // A5 heater status reply (cmd 0x71/0x73/0x74/0x81); matched by subtype reply
  // command and source address (MasterTemp and MaxE-Therm share 0x74, so the
  // src address disambiguates which heater answered).
  if (is_heater_reply_cmd(frame.cmd)) {
    for (size_t i = 0; i < this->heaters_.size(); i++) {
      if (this->heaters_[i]->address() != frame.src || !this->heaters_[i]->matches_reply_cmd(frame.cmd))
        continue;
      this->heaters_[i]->on_status(frame.data);
      this->heater_last_status_ms_[i] = millis();
      break;
    }
    return;
  }

  // A5 IntelliChem status reply (cmd 0x12), matched by source address.
  if (frame.cmd == CHEM_CMD_STATUS_REPLY) {
    for (size_t i = 0; i < this->intellichems_.size(); i++) {
      if (this->intellichems_[i]->address() != frame.src)
        continue;
      this->intellichems_[i]->on_status(frame.data);
      this->chem_last_status_ms_[i] = millis();
      break;
    }
    return;
  }
}

}  // namespace pentair
}  // namespace esphome
