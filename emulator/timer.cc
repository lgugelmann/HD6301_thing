#include "timer.h"

#include <cstdint>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace eight_bit {
namespace {
constexpr uint8_t kTimerOverflow = 0x10;
constexpr uint8_t kTimerInterruptEnable = 0x04;
}  // namespace

Timer::Timer(AddressSpace* address_space, Interrupt* interrupt)
    : address_space_(address_space), interrupt_(interrupt) {
  auto status = address_space_->register_read(
      0x0008, 0x0008, [this](uint16_t) { return read_status_register(); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register read callback for Timer status register: "
               << status;
  }
  status = address_space_->register_write(
      0x0008, 0x0008,
      [this](uint16_t, uint8_t data) { write_status_register(data); });
  if (!status.ok()) {
    LOG(ERROR)
        << "Failed to register write callback for Timer status register: "
        << status;
  }
  status = address_space_->register_read(
      0x0009, 0x0009, [this](uint16_t) { return read_counter_high(); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register read callback for Timer counter high: "
               << status;
  }
  status = address_space_->register_write(
      0x0009, 0x0009,
      [this](uint16_t, uint8_t data) { write_counter_high(data); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register write callback for Timer counter high: "
               << status;
  }
  status = address_space_->register_read(
      0x000a, 0x000a, [this](uint16_t) { return read_counter_low(); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register read callback for Timer counter low: "
               << status;
  }
  status = address_space_->register_write(
      0x000a, 0x000a,
      [this](uint16_t, uint8_t data) { write_counter_low(data); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register write callback for Timer counter low: "
               << status;
  }
}

void Timer::tick() {
  counter_ += 1;
  if (counter_ == 0) {
    VLOG(3) << "Timer overflow";
    // Counter just overflowed. Set overflow bit, and fire an interrupt if
    // enabled.
    status_register_ |= kTimerOverflow;
    if (status_register_ & kTimerInterruptEnable) {
      if (interrupt_id_ == 0) {
        VLOG(3) << "Timer interrupt";
        interrupt_id_ = interrupt_->set_interrupt();
      } else {
        VLOG(3) << "Timer interrupt already pending";
      }
    }
  }
}

uint8_t Timer::read_status_register() {
  // If the overflow bit is set, reading the status register and then the
  // counter will clear it.
  if (status_register_ & kTimerOverflow) {
    counter_read_clears_interrupt_ = true;
  };
  return status_register_;
}

void Timer::write_status_register(uint8_t value) {
  // The top 3 bits are read-only.
  status_register_ = value & 0x1f;
  VLOG(1) << absl::StreamFormat("Timer status register set to %02x",
                                status_register_);
}

uint8_t Timer::read_counter_low() {
  if (counter_low_latched_) {
    counter_low_latched_ = false;
    return counter_low_latch_;
  }
  return counter_;
}

void Timer::write_counter_low(uint8_t value) {
  if (counter_high_latched_) {
    counter_high_latched_ = false;
    counter_ = counter_high_latch_ << 8 | value;
  } else {
    counter_ = (counter_ & 0xff00) | (uint16_t)value;
  }
}

uint8_t Timer::read_counter_high() {
  uint16_t counter = counter_;
  if (counter_read_clears_interrupt_) {
    counter_read_clears_interrupt_ = false;
    status_register_ &= ~kTimerOverflow;
    if (interrupt_id_ != 0) {
      interrupt_->clear_interrupt(interrupt_id_);
      interrupt_id_ = 0;
    }
  }
  counter_low_latch_ = counter;
  counter_low_latched_ = true;
  return counter >> 8;
}

void Timer::write_counter_high(uint8_t value) {
  counter_high_latch_ = value;
  counter_high_latched_ = true;
  counter_ = 0xfff8;
}

}  // namespace eight_bit