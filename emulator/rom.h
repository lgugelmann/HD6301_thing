#ifndef EIGHT_BIT_ROM_H
#define EIGHT_BIT_ROM_H

#include <cstdint>
#include <span>

#include "address_space.h"

namespace eight_bit {

class Rom {
 public:
  Rom(AddressSpace* address_space, uint16_t base_address, uint16_t size,
      uint8_t fill_byte = 0);
  Rom(const Rom&) = delete;
  Rom& operator=(const Rom&) = delete;

  // Load data into the ROM. `address` is with respect to the base address of
  // the ROM. A value of 0 loads the data at the start of the ROM. If `data`
  // contains more bytes than the ROM can hold, the extra bytes are ignored.
  void load(uint16_t address, std::span<uint8_t> data);

  // Print ROM contents to stdout
  void hexdump() const;

 private:
  AddressSpace* address_space_;
  const uint16_t base_address_;
  std::vector<uint8_t> data_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_ROM_H