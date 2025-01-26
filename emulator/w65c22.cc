#include "w65c22.h"

#include <bit>
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
  if (shift_register_shifts_remaining_ > 0) {
    --shift_register_ticks_to_next_edge_;
    if (shift_register_ticks_to_next_edge_ == 0) {
      if (port_cb_state_ & kCb1Mask) {
        // Clock is up, so we're at the start of a shift cycle. The actual
        // hardware first lowers the clock line (CB1), then prepares the next
        // bit (CB2). We do this here in two steps to make sure we're not
        // depending on the data being ready on the falling edge already. We
        // could also push things a bit and only prepare the next bit on the
        // next tick, just before raising the clock edge again.
        port_cb_state_ &= ~kCb1Mask;
        port_cb_.write_output_register(port_cb_state_);
        // Shift out the next bit. The SR is MSB first and rotates bit 7 back
        // into bit 0 on each shift.
        static_assert(std::numeric_limits<typeof(shift_register_)>::digits ==
                      8);
        shift_register_ = std::rotl(shift_register_, 1);
        // Clock is 0 here, only need to write CB2 bit.
        port_cb_state_ = (shift_register_ & 1) << kCb2Pin;
        port_cb_.write_output_register(port_cb_state_);
        shift_register_ticks_to_next_edge_ = 1;
      } else {
        // Clock is down, so we're at the end of a shift cycle. Raise the clock
        // line (CB1) again.
        port_cb_state_ |= kCb1Mask;
        port_cb_.write_output_register(port_cb_state_);
        --shift_register_shifts_remaining_;
        if (shift_register_shifts_remaining_ == 0) {
          // Shifting is done, set the IFR bit.
          set_irq_flag(kIrqShiftRegister);
        } else {
          shift_register_ticks_to_next_edge_ = 1;
        }
      }
    }
  }
}

IOPort* W65C22::port_a() { return &port_a_; }

IOPort* W65C22::port_b() { return &port_b_; }

IOPort* W65C22::port_ca() { return &port_ca_; }

IOPort* W65C22::port_cb() { return &port_cb_; }

W65C22::W65C22(AddressSpace* address_space, uint16_t base_address,
               Interrupt* interrupt)
    : address_space_(address_space),
      base_address_(base_address),
      interrupt_(interrupt),
      port_a_("65C22 Port A"),
      port_b_("65C22 Port B"),
      port_ca_("65C22 Port CA"),
      port_cb_("65C22 Port CB") {}

absl::Status W65C22::Initialize() {
  // CA is output-only. CB is initialized when shift register ACR bits are set.
  port_ca_.write_data_direction_register(kCa1Mask | kCa2Mask);
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
      return port_b_.read_input_register();
    case kOutputRegisterA:
      return port_a_.read_input_register();
    case kDataDirectionRegisterB:
      return port_b_.read_data_direction_register();
    case kDataDirectionRegisterA:
      return port_a_.read_data_direction_register();
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
    case kShiftRegister:
      clear_irq_flag(kIrqShiftRegister);
      return shift_register_;
    case kAuxiliaryControlRegister:
      return auxiliary_control_register_;
    case kPeripheralControlRegister:
      return peripheral_control_register_;
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
      port_b_.write_output_register(value);
      break;
    case kOutputRegisterA:
      port_a_.write_output_register(value);
      break;
    case kDataDirectionRegisterB:
      port_b_.write_data_direction_register(value);
      break;
    case kDataDirectionRegisterA:
      port_a_.write_data_direction_register(value);
      break;
    case kTimer1CounterLow:
      // A write sets the low byte of the latch, it doesn't change the counter.
      timer1_latch_ = (timer1_latch_ & 0xFF00) | value;
      break;
    case kTimer1CounterHigh:
      // Setting the high byte of the latch also reloads the timer.
      timer1_latch_ = (timer1_latch_ & 0xFF) | (value << 8);
      // The timer is at N for the first cycle after the latch is set to N. Our
      // tick code decrements the counter immediately. The +1 below shifts
      // things slightly so the emulator matches the hardware.
      timer1_counter_ = timer1_latch_ + 1;
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
      // See the timer 1 high counter byte write for an explanation of the +1.
      timer2_counter_ = ((value << 8) | timer2_latch_low_) + 1;
      timer2_active_ = true;
      clear_irq_flag(kIrqTimer2);
      break;
    case kShiftRegister:
      shift_register_ = value;
      VLOG(1) << "65C22 shift register write: "
              << absl::Hex(value, absl::kZeroPad2);
      clear_irq_flag(kIrqShiftRegister);
      if ((auxiliary_control_register_ & kAcrShiftRegisterBits) ==
          kAcrShiftRegisterOutPhi2) {
        // Shift out under phi2 control
        shift_register_shifts_remaining_ = 8;
        shift_register_ticks_to_next_edge_ = 4;
      } else {
        // Warn about a SR write while it's in an unimplemented mode.
        LOG(ERROR) << "Shift register write while in unimplemented mode. ACR: "
                   << absl::Hex(auxiliary_control_register_, absl::kZeroPad2);
      }
      clear_irq_flag(kIrqShiftRegister);
      break;
    case kAuxiliaryControlRegister:
      auxiliary_control_register_ = value;
      if ((auxiliary_control_register_ & kAcrShiftRegisterBits) ==
          kAcrShiftRegisterOutPhi2) {
        // Shift out under phi2 control, set CB bits to outputs
        port_cb_.write_data_direction_register(kCb1Mask | kCb2Mask);
        port_cb_state_ = kCb1Mask;  // Clock is CB1 and high when idle
        port_cb_.write_output_register(port_cb_state_);
      }
      break;
    case kPeripheralControlRegister:
      peripheral_control_register_ = value;
      // TODO: implement more PCR bits. We only support CA2 high/low for now.
      if ((peripheral_control_register_ & kPcrCA2Bits) == kPcrCA2High) {
        port_ca_state_ |= kCa2Mask;
        port_ca_.write_output_register(port_ca_state_);
      } else if ((peripheral_control_register_ & kPcrCA2Bits) == kPcrCA2Low) {
        port_ca_state_ &= ~kCa2Mask;
        port_ca_.write_output_register(port_ca_state_);
      }
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
  // Fire the timer2 interrupt if it's not already set and is enabled.
  if (irq_flag_register_ & kIrqTimer2 && irq_enable_register_ & kIrqTimer2 &&
      timer2_interrupt_id_ == 0) {
    timer2_interrupt_id_ = interrupt_->set_interrupt();
  }
  // Fire shift register interrupt if it's not already set and is enabled.
  if (irq_flag_register_ & kIrqShiftRegister &&
      irq_enable_register_ & kIrqShiftRegister &&
      shift_register_interrupt_id_ == 0) {
    shift_register_interrupt_id_ = interrupt_->set_interrupt();
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
  if ((irq_flag_register_ & kIrqTimer1) == 0 && timer1_interrupt_id_ != 0) {
    interrupt_->clear_interrupt(timer1_interrupt_id_);
    timer1_interrupt_id_ = 0;
  }
  // Clear the timer2 interrupt if it's still outstanding.
  if ((irq_flag_register_ & kIrqTimer2) == 0 && timer2_interrupt_id_ != 0) {
    interrupt_->clear_interrupt(timer2_interrupt_id_);
    timer2_interrupt_id_ = 0;
  }
  // Clear the shift register interrupt if it's still outstanding.
  if ((irq_flag_register_ & kIrqShiftRegister) == 0 &&
      shift_register_interrupt_id_ != 0) {
    interrupt_->clear_interrupt(shift_register_interrupt_id_);
    shift_register_interrupt_id_ = 0;
  }
}

}  // namespace eight_bit