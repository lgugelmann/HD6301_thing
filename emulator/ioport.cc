#include "ioport.h"

#include <cstdio>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {

IOPort::IOPort(std::string_view name) : name_(name) {}

uint8_t IOPort::read_input_register() {
  uint8_t read_data = input_register_;
  // Bits set as outputs are returned as the last written data.
  read_data |= output_register_ & data_direction_;
  VLOG(4) << "Read from " << name_ << ": "
          << absl::Hex(read_data, absl::kZeroPad2);
  return read_data;
}

uint8_t IOPort::read_output_register() { return output_register_; }

void IOPort::write_output_register(uint8_t data) {
  output_register_ = data;
  VLOG(4) << "Write to " << name_ << ": "
          << absl::Hex(data & data_direction_, absl::kZeroPad2);
  for (const auto& callback : output_change_callbacks_) {
    callback(data & data_direction_);
  }
}

void IOPort::provide_inputs(uint8_t data, uint8_t mask) {
  input_register_ = (input_register_ & ~mask) | data;
  for (const auto& callback : input_change_callbacks_) {
    callback(input_register_);
  }
}

void IOPort::write_data_direction_register(uint8_t direction_mask) {
  VLOG(1) << "Setting direction for " << name_ << " to "
          << absl::Hex(direction_mask, absl::kZeroPad2);
  data_direction_ = direction_mask;
}

uint8_t IOPort::read_data_direction_register() const { return data_direction_; }

void IOPort::register_output_change_callback(
    const output_change_callback& callback) {
  output_change_callbacks_.push_back(callback);
}

void IOPort::register_input_change_callback(
    const input_change_callback& callback) {
  input_change_callbacks_.push_back(callback);
}

}  // namespace eight_bit
