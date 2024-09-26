#ifndef EIGHT_BIT_WD65C22_H
#define EIGHT_BIT_WD65C22_H

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "address_space.h"
#include "interrupt.h"
#include "ioport.h"

namespace eight_bit {

// Emulates a Western Design Center W65C22 Versatile Interface Adapter.
// Currently only port A/B and their DDR registers are supported, as well as
// timer 1 / timer 2 operation and interrupts.
class W65C22 {
 public:
  ~W65C22() = default;
  W65C22(const W65C22&) = delete;

  static absl::StatusOr<std::unique_ptr<W65C22>> Create(
      AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt);

  // Calling this indicates that one clock cycle has passed.
  void tick();

  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t value);

  IOPort* port_a();
  IOPort* port_b();

  // Constants for register offsets
  static constexpr uint8_t kOutputRegisterB = 0;
  static constexpr uint8_t kOutputRegisterA = 1;
  static constexpr uint8_t kDataDirectionRegisterB = 2;
  static constexpr uint8_t kDataDirectionRegisterA = 3;
  static constexpr uint8_t kTimer1CounterLow = 4;
  static constexpr uint8_t kTimer1CounterHigh = 5;
  static constexpr uint8_t kTimer1LatchLow = 6;
  static constexpr uint8_t kTimer1LatchHigh = 7;
  static constexpr uint8_t kTimer2CounterLow = 8;
  static constexpr uint8_t kTimer2LatchLow = 8;
  static constexpr uint8_t kTimer2CounterHigh = 9;
  static constexpr uint8_t kAuxiliaryControlRegister = 11;
  static constexpr uint8_t kInterruptFlagRegister = 13;
  static constexpr uint8_t kInterruptEnableRegister = 14;

  // Constants for the Interrupt Registers
  static constexpr uint8_t kIrqAny = 0x80;
  static constexpr uint8_t kIrqTimer1 = 0x40;
  static constexpr uint8_t kIrqTimer2 = 0x20;

 private:
  W65C22(AddressSpace* address_space, uint16_t base_address,
         Interrupt* interrupt);

  absl::Status Initialize();

  void set_irq_flag(uint8_t mask);
  void clear_irq_flag(uint8_t mask);

  AddressSpace* address_space_;
  uint16_t base_address_;
  Interrupt* interrupt_;
  IOPort port_a_;
  IOPort port_b_;
  uint8_t irq_enable_register_ = 0;
  uint8_t irq_flag_register_ = 0;
  uint8_t auxiliary_control_register_ = 0;
  int timer1_interrupt_id_ = 0;
  int timer2_interrupt_id_ = 0;
  // The latch is reloaded one cycle after we reached 0, not immediately. This
  // flag keeps track of that.
  bool reload_timer1_latch_ = false;
  // Whether or not a timer is active. This is true after the latch is set and
  // until the timer reaches 0 in one-shot mode.
  bool timer1_active_ = false;
  bool timer2_active_ = false;
  uint16_t timer1_latch_ = 0xffff;
  uint8_t timer2_latch_low_ = 0xff;
  uint16_t timer1_counter_ = 0;
  uint16_t timer2_counter_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_WD65C22_H