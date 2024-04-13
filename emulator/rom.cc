#include "rom.h"

#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

#include "address_space.h"
#include "hexdump.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<Rom>> Rom::create(AddressSpace* address_space,
                                                 uint16_t base_address,
                                                 uint16_t size,
                                                 uint8_t fill_byte) {
  std::unique_ptr<Rom> rom(
      new Rom(address_space, base_address, size, fill_byte));
  auto status = rom->initialize();
  if (!status.ok()) {
    return status;
  }
  return rom;
}

void Rom::load(uint16_t address, std::span<uint8_t> data) {
  for (uint16_t i = address; i < address + data.size() && i < data_.size();
       ++i) {
    data_[i] = data[i - address];
  }
}

void Rom::hexdump() const { std::cout << eight_bit::hexdump(data_) << "\n"; }

Rom::Rom(AddressSpace* address_space, uint16_t base_address, uint16_t size,
         uint8_t fill_byte)
    : address_space_(address_space),
      base_address_(base_address),
      data_(size, fill_byte) {}

absl::Status Rom::initialize() {
  auto status = address_space_->register_read(
      base_address_, base_address_ + data_.size() - 1,
      [this](uint16_t address) -> uint8_t {
        return data_[address - base_address_];
      });
  return status;
}

}  // namespace eight_bit