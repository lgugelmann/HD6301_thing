#ifndef EIGHT_BIT_TIMER_H
#define EIGHT_BIT_TIMER_H

#include <cstdint>

#include "address_space.h"
#include "interrupt.h"

namespace eight_bit {

class Timer {
 public:
  Timer(AddressSpace* address_space, Interrupt* interrupt);
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  ~Timer() = default;

  // Increment the counter.
  void tick();

  // Read/write access for the status register. The top 3 bits of the status
  // register are read-only and are never changed on writes.
  uint8_t read_status_register();
  void write_status_register(uint8_t value);

  // Read/write access for counter bytes. See the comments below for the
  // latching and interrupt clearing logic.
  uint8_t read_counter_low();
  void write_counter_low(uint8_t value);
  uint8_t read_counter_high();
  void write_counter_high(uint8_t value);

 private:
  AddressSpace* address_space_ = nullptr;
  Interrupt* interrupt_ = nullptr;

  uint8_t status_register_ = 0;

  // After reading the status register with the interrupt bit set, a subsequent
  // read of the counter high byte will clear the interrupt bit.
  bool counter_read_clears_interrupt_ = false;

  // Reads to the high byte cause the low byte to be latched. The next read
  // returns the latched value.
  bool counter_low_latched_ = false;
  uint8_t counter_low_latch_ = 0;

  // When the high byte is written, the counter is reset to 0xfff8 regardless of
  // the value written. When the low byte is written to next, the actual latched
  // value is written to the counter.
  bool counter_high_latched_ = false;
  uint8_t counter_high_latch_ = 0;

  // Tick counter
  uint16_t counter_ = 0;

  // The interrupt ID of the pending interrupt, or 0 if no interrupt is pending.
  int interrupt_id_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_TIMER_H