#pragma once
#include "protocol.h"
#include "pentair_pump.h"
#include "pentair_chlorinator.h"
#include "pentair_heater.h"
#include "pentair_intellichem.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include <vector>

namespace esphome {
namespace pentair {

// Bus master. Emulates the IntelliCenter OCP role: owns the RS-485 segment,
// polls each pump on a fixed cadence, continuously commands the chlorinator
// output (flow-gated), and parses inbound telemetry.
class PentairHub : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_source_address(uint8_t addr) { this->source_address_ = addr; }
  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
  void set_active_poll_interval(uint32_t ms) { this->active_poll_interval_ms_ = ms; }
  void set_tx_gap(uint32_t ms) { this->tx_gap_ms_ = ms; }
  void set_require_pump_flow(bool require) { this->require_pump_flow_ = require; }

  void register_pump(PentairPump *pump) {
    this->pumps_.push_back(pump);
    this->pump_last_status_ms_.push_back(0);
  }
  void register_chlorinator(PentairChlorinator *chlor) {
    this->chlorinators_.push_back(chlor);
    this->chlor_last_status_ms_.push_back(0);
  }
  void register_heater(PentairHeater *heater) {
    this->heaters_.push_back(heater);
    this->heater_last_status_ms_.push_back(0);
    this->heater_last_poll_ms_.push_back(0);
  }
  void register_intellichem(PentairIntelliChem *chem) {
    this->intellichems_.push_back(chem);
    this->chem_last_status_ms_.push_back(0);
    this->chem_last_poll_ms_.push_back(0);
  }

 protected:
  // TX ------------------------------------------------------------------
  void enqueue_frame_(const std::vector<uint8_t> &frame);
  void send_next_frame_();
  void queue_pump_poll_(PentairPump *pump);
  void queue_pump_status_(PentairPump *pump);
  void queue_chlor_command_(PentairChlorinator *chlor);
  void queue_heater_poll_(PentairHeater *heater);
  void queue_chem_poll_(PentairIntelliChem *chem);
  void queue_chem_set_(PentairIntelliChem *chem);

  // RX ------------------------------------------------------------------
  void read_uart_();
  void parse_buffer_();
  bool try_parse_a5_(size_t start);    // returns true if a frame was consumed
  bool try_parse_chlor_(size_t start);
  void dispatch_(const RxFrame &frame);

  // Flow gate: is any registered pump reporting that it is running?
  bool flow_present_();

  uint8_t source_address_{ADDR_CONTROLLER_DEFAULT};
  GPIOPin *flow_control_pin_{nullptr};
  uint32_t poll_interval_ms_{16000};
  uint32_t active_poll_interval_ms_{2000};
  uint32_t tx_gap_ms_{60};
  bool require_pump_flow_{true};

  std::vector<PentairPump *> pumps_;
  std::vector<PentairChlorinator *> chlorinators_;
  std::vector<PentairHeater *> heaters_;
  std::vector<PentairIntelliChem *> intellichems_;
  std::vector<uint32_t> pump_last_status_ms_;
  std::vector<uint32_t> chlor_last_status_ms_;
  // Slow devices carry their own firmware cadence, so they track both when they
  // last replied (presence) and when they were last polled (scheduling).
  std::vector<uint32_t> heater_last_status_ms_;
  std::vector<uint32_t> heater_last_poll_ms_;
  std::vector<uint32_t> chem_last_status_ms_;
  std::vector<uint32_t> chem_last_poll_ms_;

  std::vector<std::vector<uint8_t>> tx_queue_;
  uint32_t last_tx_ms_{0};
  uint32_t last_poll_ms_{0};

  std::vector<uint8_t> rx_buffer_;
  uint32_t last_rx_byte_ms_{0};
};

}  // namespace pentair
}  // namespace esphome
