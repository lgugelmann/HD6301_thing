#include "address_space.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "hexdump.h"

namespace eight_bit {

absl::Status AddressSpace::register_read(uint16_t start, uint16_t end,
                                         read_callback callback) {
  for (const auto& r : read_ranges_) {
    if (r.start <= end && r.end >= start) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Address range %04x-%04x already registered for reads", start, end));
    }
  }
  read_ranges_.push_back({start, end});
  uint8_t index = static_cast<uint8_t>(read_callbacks_.size());
  read_callbacks_.push_back(std::move(callback));
  std::fill(read_lookup_.begin() + start,
            read_lookup_.begin() + static_cast<uint32_t>(end) + 1, index);
  return absl::OkStatus();
}

absl::Status AddressSpace::register_write(uint16_t start, uint16_t end,
                                          write_callback callback) {
  for (const auto& r : write_ranges_) {
    if (r.start <= end && r.end >= start) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Address range %04x-%04x already registered for writes", start, end));
    }
  }
  write_ranges_.push_back({start, end});
  uint8_t index = static_cast<uint8_t>(write_callbacks_.size());
  write_callbacks_.push_back(std::move(callback));
  std::fill(write_lookup_.begin() + start,
            write_lookup_.begin() + static_cast<uint32_t>(end) + 1, index);
  return absl::OkStatus();
}

uint8_t AddressSpace::get(uint16_t address) {
  VLOG(5) << absl::StreamFormat("Reading from address %04x", address);
  uint8_t idx = read_lookup_[address];
  if (idx != 0) {
    return read_callbacks_[idx](address);
  }
  LOG(ERROR) << absl::StreamFormat("No read callback for address %04x",
                                   address);
  return 0;
}

uint16_t AddressSpace::get16(uint16_t address) {
  if (address > 0xfffe) {
    LOG(ERROR) << absl::StreamFormat("Invalid 16-bit read from address %04x",
                                     address);
    return 0;
  }
  return (uint16_t)get(address) << 8 | (uint16_t)get(address + 1);
}

void AddressSpace::set(uint16_t address, uint8_t data) {
  VLOG(5) << absl::StreamFormat("Writing to address %04x: %02x", address, data);
  uint8_t idx = write_lookup_[address];
  if (idx != 0) {
    write_callbacks_[idx](address, data);
    return;
  }
  LOG(ERROR) << absl::StreamFormat("No write callback for address %04x",
                                   address);
}

void AddressSpace::set16(uint16_t address, uint16_t data) {
  if (address > 0xfffe) {
    LOG(ERROR) << absl::StreamFormat("Invalid 16-bit write to address %04x",
                                     address);
    return;
  }
  set(address, data >> 8);
  set(address + 1, data);
}

}  // namespace eight_bit
