#ifndef EIGHT_BIT_WD65C22_H
#define EIGHT_BIT_WD65C22_H

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "address_space.h"
#include "ioport.h"

namespace eight_bit {

// Emulates a Western Digital 65C22 Versatile Interface Adapter. Currently only
// port A/B and their DDR registers are supported.
class WD65C22 {
 public:
  ~WD65C22() = default;
  WD65C22(const WD65C22&) = delete;

  static absl::StatusOr<std::unique_ptr<WD65C22>> Create(
      AddressSpace* address_space, uint16_t base_address);

  IOPort* port_a();
  IOPort* port_b();

 private:
  WD65C22(AddressSpace* address_space, uint16_t base_address);

  absl::Status Initialize();

  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t value);

  AddressSpace* address_space_;
  uint16_t base_address_;
  IOPort port_a_;
  IOPort port_b_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_WD65C22_H