#ifndef EIGHT_BIT_RAM_H
#define EIGHT_BIT_RAM_H

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "address_space.h"

namespace eight_bit {

class Ram {
 public:
  Ram(const Ram&) = delete;
  Ram& operator=(const Ram&) = delete;

  static absl::StatusOr<std::unique_ptr<Ram>> create(
      AddressSpace* address_space, uint16_t base_address, uint16_t size,
      uint8_t fill_byte = 0);

  // Print RAM contents to stdout
  void hexdump() const;

 private:
  Ram(AddressSpace* address_space, uint16_t base_address, uint16_t size,
      uint8_t fill_byte = 0);

  absl::Status initialize();

  AddressSpace* address_space_;
  const uint16_t base_address_;
  std::vector<uint8_t> data_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_RAM_H