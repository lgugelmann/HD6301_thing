#include "w65c22.h"

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {

constexpr uint8_t kAcrTimer1Continuous = 0x40;

absl::StatusOr<std::unique_ptr<W65C22>> W65C22::Create(
    AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt) {
  auto wd65c22 = std::unique_ptr<W65C22>(
      new W65C22(address_space, base_address, interrupt));
  auto status = wd65c22->Initialize();
  if (!status.ok()) {
    return status;
  }
  return wd65c22;
}

void W65C22::tick() {
  --timer1_counter_;
  --timer2_counter_;
  if (reload_timer1_latch_) {
    // reload the timer with the latch value. This is done after one cycle of
    // delay.
    timer1_counter_ = timer1_latch_;
    reload_timer1_latch_ = false;
  }
  if (timer1_counter_ == 0) {
    // Timer 1 has expired. Set the IRQ flag if the timer is active, i.e. in
    // continuous mode, or on its first pass through 0 in one-shot mode.
    if (timer1_active_) {
      // This also fires the interrupt
      set_irq_flag(kIrqTimer1);
    }
    // Timer 1 always reloads from the latched value, regardless of one-shot or
    // continuous.
    reload_timer1_latch_ = true;

    // Keep the timer active in continuous mode, stop it in one-shot mode.
    timer1_active_ = auxiliary_control_register_ & kAcrTimer1Continuous;
  }
  if (timer2_counter_ == 0) {
    if (timer2_active_) {
      // This also fires the interrupt
      set_irq_flag(kIrqTimer2);
      timer2_active_ = false;
    }
  }
}

IOPort* W65C22::port_a() { return &port_a_; }

IOPort* W65C22::port_b() { return &port_b_; }

W65C22::W65C22(AddressSpace* address_space, uint16_t base_address,
               Interrupt* interrupt)
    : address_space_(address_space),
      base_address_(base_address),
      interrupt_(interrupt),
      port_a_("65C22 Port A"),
      port_b_("65C22 Port B") {}

absl::Status W65C22::Initialize() {
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

uint8_t W65C22::read(uint16_t address) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case kOutputRegisterB:
      return port_b_.read();
    case kOutputRegisterA:
      return port_a_.read();
    case kDataDirectionRegisterB:
      return port_b_.get_direction();
    case kDataDirectionRegisterA:
      return port_a_.get_direction();
    case kTimer1CounterLow:
      // Reading this also clears the timer 1 IFR bit.
      clear_irq_flag(kIrqTimer1);
      return timer1_counter_ & 0xFF;
    case kTimer1CounterHigh:
      // Timer 1 high byte
      return timer1_counter_ >> 8;
    case kTimer1LatchLow:
      // Timer 1 latch low byte
      return timer1_latch_ & 0xFF;
    case kTimer1LatchHigh:
      // Timer 1 latch high byte
      return timer1_latch_ >> 8;
    case kTimer2CounterLow:
      // Clear the timer 2 IFR bit when reading the counter.
      clear_irq_flag(kIrqTimer2);
      return timer2_counter_ & 0xFF;
    case kTimer2CounterHigh:
      // Clear the timer 2 IFR bit when reading the counter.
      clear_irq_flag(kIrqTimer2);
      return timer2_counter_ >> 8;
    case kAuxiliaryControlRegister:
      return auxiliary_control_register_;
    case kInterruptEnableRegister:
      return irq_enable_register_;
    case kInterruptFlagRegister:
      return irq_flag_register_;
    default:
      LOG(ERROR) << "Read from unimplemented 65C22 register: "
                 << absl::Hex(offset, absl::kZeroPad2);
      return 0;
  }
}

void W65C22::write(uint16_t address, uint8_t value) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case kOutputRegisterB:
      port_b_.write(value);
      break;
    case kOutputRegisterA:
      port_a_.write(value);
      break;
    case kDataDirectionRegisterB:
      port_b_.set_direction(value);
      break;
    case kDataDirectionRegisterA:
      port_a_.set_direction(value);
      break;
    case kTimer1CounterLow:
      // A write sets the low byte of the latch, it doesn't change the counter.
      timer1_latch_ = (timer1_latch_ & 0xFF00) | value;
      break;
    case kTimer1CounterHigh:
      // Setting the high byte of the latch also reloads the timer.
      timer1_latch_ = (timer1_latch_ & 0xFF) | (value << 8);
      timer1_counter_ = timer1_latch_;
      timer1_active_ = true;
      clear_irq_flag(kIrqTimer1);
      break;
    case kTimer1LatchLow:
      // Timer 1 latch low byte
      timer1_latch_ = (timer1_latch_ & 0xFF00) | value;
      break;
    case kTimer1LatchHigh:
      // Timer 1 latch high byte. No transfer to the counter here, but the IFR
      // flag is cleared.
      timer1_latch_ = (timer1_latch_ & 0xFF) | (value << 8);
      clear_irq_flag(kIrqTimer1);
      break;
    case kTimer2LatchLow:
      timer2_latch_low_ = value;
      break;
    case kTimer2CounterHigh:
      timer2_counter_ = (value << 8) | timer2_latch_low_;
      timer2_active_ = true;
      clear_irq_flag(kIrqTimer2);
      break;
    case kAuxiliaryControlRegister:
      auxiliary_control_register_ = value;
      break;
    case kInterruptFlagRegister:
      // The datasheet isn't explicit on this, but it seems that one can clear
      // IRQ flags by writing to the register. Only timer 1 is documented as
      // having this behavior though.
      clear_irq_flag(value & 0x7f);
      break;
    case kInterruptEnableRegister:
      // If bit 7 is 0, then the bits set in value are cleared in the IRQ enable
      // register. Otherwise they are set. Bits that are 0 are always left
      // untouched.
      if (value & 0x80) {
        irq_enable_register_ |= value & 0x7F;
        // Some IRQs that had previous flags set might now need to fire. The IFR
        // top bit also needs to be recomputed. set_irq_flag(0) does this.
        set_irq_flag(0);
      } else {
        irq_enable_register_ &= (~value) & 0x7F;
        // Clear any IRQs that are no longer enabled and recompute the top IFR
        // bit.
        clear_irq_flag(0);
      }
      // This recomputes the

      break;
    default:
      LOG(ERROR) << "Write to unimplemented 65C22 register: "
                 << absl::Hex(offset, absl::kZeroPad2);
      break;
  }
}

void W65C22::set_irq_flag(uint8_t mask) {
  irq_flag_register_ |= (mask & 0x7F);
  if ((irq_flag_register_ & 0x7F & irq_enable_register_) != 0) {
    // If any of the other bits are set, and the corresponding IRQ is enabled,
    // then the top bit is set.
    irq_flag_register_ |= 0x80;
  }
  // Fire the timer1 interrupt if it's not already set and is enabled.
  if (irq_flag_register_ & kIrqTimer1 && irq_enable_register_ & kIrqTimer1 &&
      timer1_interrupt_id_ == 0) {
    timer1_interrupt_id_ = interrupt_->set_interrupt();
  }
}

void eight_bit::W65C22::clear_irq_flag(uint8_t mask) {
  // Clears out the mask bits and the top bit.
  irq_flag_register_ &= ~(mask | 0x80);
  if ((irq_flag_register_ & irq_enable_register_) != 0) {
    // If any of the other bits are set, the top bit is set again.
    irq_flag_register_ |= 0x80;
  }
  // Clear the timer1 interrupt if it's still outstanding.
  if (irq_flag_register_ & kIrqTimer1 && timer1_interrupt_id_ != 0) {
    interrupt_->clear_interrupt(timer1_interrupt_id_);
    timer1_interrupt_id_ = 0;
  }
}

}  // namespace eight_bit