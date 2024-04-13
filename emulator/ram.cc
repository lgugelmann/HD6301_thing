#include "ram.h"

#include <absl/log/log.h>
#include <absl/status/status.h>

#include <cstdint>
#include <iostream>

#include "address_space.h"
#include "hexdump.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<Ram>> Ram::create(AddressSpace* address_space,
                                                 uint16_t base_address,
                                                 uint16_t size,
                                                 uint8_t fill_byte) {
  std::unique_ptr<Ram> ram(
      new Ram(address_space, base_address, size, fill_byte));
  auto status = ram->initialize();
  if (!status.ok()) {
    return status;
  }
  return ram;
}

void Ram::hexdump() const { std::cout << eight_bit::hexdump(data_); }

Ram::Ram(AddressSpace* address_space, uint16_t base_address, uint16_t size,
         uint8_t fill_byte)
    : address_space_(address_space),
      base_address_(base_address),
      data_(size, fill_byte) {}

absl::Status Ram::initialize() {
  auto status = address_space_->register_read(
      base_address_, base_address_ + data_.size() - 1,
      [this](uint16_t address) -> uint8_t {
        return data_[address - base_address_];
      });
  if (!status.ok()) {
    return status;
  }
  status = address_space_->register_write(
      base_address_, base_address_ + data_.size() - 1,
      [this](uint16_t address, uint8_t data) {
        data_[address - base_address_] = data;
      });
  return status;
}

}  // namespace eight_bit