#include "ram.h"

#include <absl/log/log.h>
#include <absl/status/status.h>

#include <cstdint>
#include <iostream>

#include "address_space.h"
#include "hexdump.h"

namespace eight_bit {

Ram::Ram(AddressSpace* address_space, uint16_t base_address, uint16_t size,
         uint8_t fill_byte)
    : address_space_(address_space),
      base_address_(base_address),
      data_(size, fill_byte) {
  auto status =
      address_space_->register_read(base_address, base_address + size - 1,
                                    [this](uint16_t address) -> uint8_t {
                                      return data_[address - base_address_];
                                    });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register read callback for RAM: " << status;
  }
  status =
      address_space_->register_write(base_address, base_address + size - 1,
                                     [this](uint16_t address, uint8_t data) {
                                       data_[address - base_address_] = data;
                                     });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register write callback for RAM: " << status;
  }
}

void Ram::hexdump() const { std::cout << eight_bit::hexdump(data_); }

}  // namespace eight_bit