#include "rom.h"

#include <cstdint>
#include <span>

#include "address_space.h"

namespace eight_bit {

Rom::Rom(AddressSpace* address_space, uint16_t base_address, uint16_t size,
         uint8_t fill_byte)
    : address_space_(address_space),
      base_address_(base_address),
      data_(size, fill_byte) {
  address_space_->register_read(base_address, base_address + size - 1,
                                [this](uint16_t address) -> uint8_t {
                                  return data_[address - base_address_];
                                });
}

void Rom::load(uint16_t address, std::span<uint8_t> data) {
  for (uint16_t i = address; i < address + data.size() && i < data_.size();
       ++i) {
    data_[i] = data[i - address];
  }
}

void Rom::hexdump() const {}

}  // namespace eight_bit