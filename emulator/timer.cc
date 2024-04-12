#include "timer.h"

#include <absl/log/log.h>
#include <absl/strings/str_format.h>

#include <cstdint>

namespace eight_bit {
namespace {
constexpr uint8_t kTimerOverflow = 0x10;
constexpr uint8_t kTimerInterruptEnable = 0x04;
}  // namespace

Timer::Timer(AddressSpace* address_space, u_int16_t base_address,
             Interrupt* interrupt)
    : address_space_(address_space),
      base_address_(base_address),
      interrupt_(interrupt) {
  address_space_->register_read(
      0x0008, 0x0008, [this](uint16_t) { return read_status_register(); });
  address_space_->register_write(
      0x0008, 0x0008,
      [this](uint16_t, uint8_t data) { write_status_register(data); });
  address_space_->register_read(
      0x0009, 0x0009, [this](uint16_t) { return read_counter_high(); });
  address_space_->register_write(
      0x0009, 0x0009,
      [this](uint16_t, uint8_t data) { write_counter_high(data); });
  address_space_->register_read(
      0x000a, 0x000a, [this](uint16_t) { return read_counter_low(); });
  address_space_->register_write(
      0x000a, 0x000a,
      [this](uint16_t, uint8_t data) { write_counter_low(data); });
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

void Timer::write_counter_low(uint8_t data) {
  if (counter_high_latched_) {
    counter_high_latched_ = false;
    counter_ = counter_high_latch_ << 8 | data;
  } else {
    counter_ = (counter_ & 0xff00) | (uint16_t)data;
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