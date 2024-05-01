#include "wd65c22.h"

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<WD65C22>> WD65C22::Create(
    AddressSpace* address_space, uint16_t base_address) {
  auto wd65c22 =
      std::unique_ptr<WD65C22>(new WD65C22(address_space, base_address));
  auto status = wd65c22->Initialize();
  if (!status.ok()) {
    return status;
  }
  return wd65c22;
}

IOPort* WD65C22::port_a() { return &port_a_; }

IOPort* WD65C22::port_b() { return &port_b_; }

WD65C22::WD65C22(AddressSpace* address_space, uint16_t base_address)
    : address_space_(address_space),
      base_address_(base_address),
      port_a_("65C22 Port A"),
      port_b_("65C22 Port B") {}

absl::Status WD65C22::Initialize() {
  auto status = address_space_->register_read(
      base_address_, base_address_ + 15,
      [this](uint16_t address) { return read(address); });
  if (!status.ok()) {
    return status;
  }
  status = address_space_->register_write(
      base_address_, base_address_ + 15,
      [this](uint16_t address, uint8_t value) { write(address, value); });
  return status;
}

uint8_t WD65C22::read(uint16_t address) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case 0:
      return port_b_.read();
    case 1:
      return port_a_.read();
    case 2:
      return port_b_.get_direction();
    case 3:
      return port_a_.get_direction();
    default:
      LOG(ERROR) << "Read from unimplemented 65C22 register: "
                 << absl::Hex(offset, absl::kZeroPad2);
      return 0;
  }
}

void WD65C22::write(uint16_t address, uint8_t value) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case 0:
      port_b_.write(value);
      break;
    case 1:
      port_a_.write(value);
      break;
    case 2:
      port_b_.set_direction(value);
      break;
    case 3:
      port_a_.set_direction(value);
      break;
    default:
      LOG(ERROR) << "Write to unimplemented 65C22 register: "
                 << absl::Hex(offset, absl::kZeroPad2);
      break;
  }
}

}  // namespace eight_bit