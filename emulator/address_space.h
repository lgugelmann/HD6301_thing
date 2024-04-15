#ifndef EIGHT_BIT_ADDRESS_SPACE_H
#define EIGHT_BIT_ADDRESS_SPACE_H

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "absl/status/status.h"

namespace eight_bit {

// Represents a 16-bit-sized address space with single-byte addressing and
// two-byte values stored MSB first.
class AddressSpace {
 public:
  AddressSpace() = default;
  AddressSpace(const AddressSpace&) = delete;
  AddressSpace& operator=(const AddressSpace&) = delete;
  ~AddressSpace() = default;

  typedef std::function<uint8_t(uint16_t)> read_callback;
  typedef std::function<void(uint16_t, uint8_t)> write_callback;

  // Registers a read callback for a given address range. Returns an error if
  // any part of the range is already registered for reads. Start and end are
  // inclusive.
  absl::Status register_read(uint16_t start, uint16_t end,
                             read_callback callback);

  // Registers a write callback for a given address range. Returns an error if
  // any part of the range is already registered for reads. Start and end are
  // inclusive.
  absl::Status register_write(uint16_t start, uint16_t end,
                              write_callback callback);

  // Returns the byte at address `address`.
  uint8_t get(uint16_t address);

  // Returns the two bytes at address `address`, MSB first.
  uint16_t get16(uint16_t address);

  // Sets the memory at address `address` to `data`.
  void set(uint16_t address, uint8_t data);

  // Sets the memory at address `address` to `data`, MSB first.
  void set16(uint16_t address, uint16_t data);

 private:
  struct ReadAddressRange {
    uint16_t start;
    uint16_t end;
    read_callback callback;
  };
  struct WriteAddressRange {
    uint16_t start;
    uint16_t end;
    write_callback callback;
  };

  std::vector<ReadAddressRange> read_ranges_;
  std::vector<WriteAddressRange> write_ranges_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_ADDRESS_SPACE_H
