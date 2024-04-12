#include "ioport.h"

#include <absl/log/log.h>
#include <absl/strings/str_cat.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace eight_bit {

IOPort::IOPort(std::string_view name) : name_(name) {}

void IOPort::write(uint8_t data) {
  data_ = data;
  VLOG(3) << "Write to " << name_ << ": "
          << absl::Hex(data & data_direction_, absl::kZeroPad2);
  for (const auto& callback : write_callbacks_) {
    callback(data & data_direction_);
  }
}

uint8_t IOPort::read() {
  uint8_t read_data = 0;
  for (const auto& callback : read_callbacks_) {
    read_data |= callback();
  }
  // Bits set as outputs are returned as the last written data.
  read_data |= data_ & data_direction_;
  VLOG(3) << "Read from " << name_ << ": "
          << absl::Hex(read_data, absl::kZeroPad2);
  return read_data;
}

void IOPort::set_direction(uint8_t direction_mask) {
  VLOG(1) << "Setting direction for " << name_ << " to "
          << absl::Hex(direction_mask, absl::kZeroPad2);
  data_direction_ = direction_mask;
}

uint8_t IOPort::get_direction() const { return data_direction_; }

void IOPort::register_read_callback(const read_callback& callback) {
  read_callbacks_.push_back(callback);
}

void IOPort::register_write_callback(const write_callback& callback) {
  write_callbacks_.push_back(callback);
}

}  // namespace eight_bit