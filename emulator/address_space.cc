#include "address_space.h"

#include <absl/log/log.h>
#include <absl/strings/str_format.h>

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "hexdump.h"

namespace eight_bit {

bool AddressSpace::register_read(uint16_t start, uint16_t end,
                                 read_callback callback) {
  ReadAddressRange range = {start, end, callback};
  for (const auto& r : read_ranges_) {
    if (r.start <= range.end && r.end >= range.start) {
      LOG(ERROR) << absl::StreamFormat(
          "Address range %04x-%04x already registered for reads", start, end);
      return false;
    }
  }
  read_ranges_.push_back(range);
  return true;
}

bool AddressSpace::register_write(uint16_t start, uint16_t end,
                                  write_callback callback) {
  WriteAddressRange range = {start, end, callback};
  for (const auto& r : write_ranges_) {
    if (r.start <= range.end && r.end >= range.start) {
      LOG(ERROR) << absl::StreamFormat(
          "Address range %04x-%04x already registered for writes", start, end);
      return false;
    }
  }
  write_ranges_.push_back(range);
  return true;
}

uint8_t AddressSpace::get(uint16_t address) {
  VLOG(5) << absl::StreamFormat("Reading from address %04x", address);
  for (const auto& r : read_ranges_) {
    if (r.start <= address && r.end >= address) {
      return r.callback(address);
    }
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
  for (const auto& r : write_ranges_) {
    if (r.start <= address && address <= r.end) {
      r.callback(address, data);
      return;
    }
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
