#ifndef EIGHT_BIT_ADDRESS_SPACE_H
#define EIGHT_BIT_ADDRESS_SPACE_H

#include <cstdint>
#include <span>
#include <vector>

namespace eight_bit {

// Represents a 16-bit-sized address space with single-byte addressing and
// two-byte values stored MSB first.
class AddressSpace {
 public:
  AddressSpace() : data_(0x10000, 0) {}

  // Returns the byte at address `address`.
  uint8_t get(uint16_t address);

  // Returns the two bytes at address `address`, MSB first.
  uint16_t get16(uint16_t address);

  // Sets the memory at address `address` to `data`.
  void set(uint16_t address, uint8_t data);

  // Sets the memory at address `address` to `data`, MSB first.
  void set16(uint16_t address, uint16_t data);

  // Load the contenst of data starting at `address`.
  void load(uint16_t address, std::span<uint8_t> data);

  // Prints the contents of the whole address space to stdout.
  void hexdump();

 private:
  std::vector<uint8_t> data_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_ADDRESS_SPACE_H
