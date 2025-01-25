#include "w65c22.h"

#include <gtest/gtest.h>

#include "address_space.h"

namespace eight_bit {
namespace {

class W65C22Test : public ::testing::Test {
 protected:
  W65C22Test() = default;

  void SetUp() override {
    auto wdc_or = W65C22::Create(&address_space_, 0, &irq_);
    ASSERT_TRUE(wdc_or.ok());
    w65c22_ = std::move(wdc_or.value());
  }

  void ClearAllFlags() {
    // Top bit clear -> clear all set flags.
    w65c22_->write(W65C22::kInterruptFlagRegister, 0x7f);
  }

  AddressSpace address_space_;
  Interrupt irq_;
  std::unique_ptr<W65C22> w65c22_;
};

TEST_F(W65C22Test, Timer1LatchesReturnTheValueWrittenToThem) {
  w65c22_->write(W65C22::kTimer1LatchLow, 0x12);
  w65c22_->write(W65C22::kTimer1LatchHigh, 0x42);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1LatchLow), 0x12);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1LatchHigh), 0x42);
}

TEST_F(W65C22Test, Timer1CounterSetOnlyByHighCounterWrite) {
  uint8_t counter_low = w65c22_->read(W65C22::kTimer1CounterLow);
  uint8_t counter_high = w65c22_->read(W65C22::kTimer1CounterHigh);

  w65c22_->write(W65C22::kTimer1LatchLow, counter_low + 1);
  // Timer 1 low latch write should not affect the counter.
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterLow), counter_low);

  // A high *latch* write should also not affect the counter.
  w65c22_->write(W65C22::kTimer1LatchHigh, counter_high + 1);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterHigh), counter_high);

  // A high *counter* write should now move the latches to the counter.
  w65c22_->write(W65C22::kTimer1CounterHigh, counter_high + 2);
  w65c22_->tick();

  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterLow), counter_low + 1);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterHigh), counter_high + 2);
}

TEST_F(W65C22Test, Timer1LowCounterWriteSetsLowLatch) {
  uint8_t counter_low = w65c22_->read(W65C22::kTimer1CounterLow);
  // A write to the low counter is the same as a write to the low latch.
  w65c22_->write(W65C22::kTimer1CounterLow, counter_low + 1);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1LatchLow), counter_low + 1);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterLow), counter_low);
}

TEST_F(W65C22Test, Timer1TickReducesCounterValue) {
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x01);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterLow), 0x00);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterHigh), 0x01);
  w65c22_->tick();
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterLow), 0xff);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer1CounterHigh), 0x00);
}

TEST_F(W65C22Test, Timer1InterruptFlagSetWhenCounterReachesZero) {
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            0);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);
}

TEST_F(W65C22Test, Timer1InterruptFlagClearedWhenCounterRead) {
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);
  w65c22_->read(W65C22::kTimer1CounterLow);
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            0);
}

TEST_F(W65C22Test, IERWriteWithTopBitSetEnablesCorrepsondingFlags) {
  // At startup no interrupts are enabled
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister), 0);

  // Note: the top bit of the register is not set
  w65c22_->write(W65C22::kInterruptEnableRegister,
                 W65C22::kIrqTimer1 | W65C22::kIrqTimer2);
  // Top bit not set should result in no changes in the IER
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister), 0);

  w65c22_->write(W65C22::kInterruptEnableRegister,
                 W65C22::kIrqTimer1 | W65C22::kIrqTimer2 | 0x80);
  // With the top bit the IER is modified
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer1 | W65C22::kIrqTimer2);
}

TEST_F(W65C22Test, IERWriteWithTopBitClearedClearsCorrepsondingFlags) {
  // Set up two interrupt enables
  w65c22_->write(W65C22::kInterruptEnableRegister,
                 W65C22::kIrqTimer1 | W65C22::kIrqTimer2 | 0x80);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer1 | W65C22::kIrqTimer2);

  w65c22_->write(W65C22::kInterruptEnableRegister, W65C22::kIrqTimer1);
  // Top bit not set should clear the corresponding IER bit
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer2);
}

TEST_F(W65C22Test, AnyInterruptFlagNotSetIfInterruptNotEnabled) {
  // Make sure no interrupts are enabled
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister), 0);
  // No flag set, top bit of IFR not set either
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister), 0);

  // Set up an interrupt flag
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  w65c22_->tick();  // 1 on the counter
  w65c22_->tick();  // 0
  // Timer 1 flag is now set
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);

  // But the top bit of IFR must not be set because timer 1 IRQ is not enabled
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqAny, 0);
}

TEST_F(W65C22Test, AnyInterruptFlagSetIfInterruptEnabled) {
  // Enable timer 1 IRQ
  w65c22_->write(W65C22::kInterruptEnableRegister, W65C22::kIrqTimer1 | 0x80);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer1);
  // Set up an interrupt flag
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  // Timer 1 flag is now set
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);

  // The top bit of IFR must also be set because timer 1 IRQ is enabled
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqAny,
            W65C22::kIrqAny);

  // The top bit must clear if we clear the corresponding IER flag as there are
  // no other interrupts.
  w65c22_->write(W65C22::kInterruptEnableRegister, W65C22::kIrqTimer1);

  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqAny, 0);
}

TEST_F(W65C22Test, Timer1InterruptFiresIfEnabled) {
  // Enable timer 1 IRQ
  w65c22_->write(W65C22::kInterruptEnableRegister, W65C22::kIrqTimer1 | 0x80);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer1);
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  // Make sure the interrupt hasn't fired yet
  ASSERT_FALSE(irq_.has_interrupt());
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0

  EXPECT_TRUE(irq_.has_interrupt());
}

TEST_F(W65C22Test, Timer1InterruptClearedOnCounteRead) {
  // Enable timer 1 IRQ
  w65c22_->write(W65C22::kInterruptEnableRegister, W65C22::kIrqTimer1 | 0x80);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptEnableRegister),
            W65C22::kIrqTimer1);
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  // Make sure the interrupt hasn't fired yet
  ASSERT_FALSE(irq_.has_interrupt());
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_TRUE(irq_.has_interrupt());
  // Clear the interrupt by reading the low counter
  w65c22_->read(W65C22::kTimer1CounterLow);

  EXPECT_FALSE(irq_.has_interrupt());
}

TEST_F(W65C22Test, InterruptsClearedViaIFRWrites) {
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);
  // Double-check that reading the IFR doesn't clear anything
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);
  // w65c22_->write(W65C22::kInterruptFlagRegister, )
}

TEST_F(W65C22Test, SingleShotTimerSetsFlagOnlyOnce) {
  // Clear all ACR bits, among other things puts Timer 1 in single-shot mode.
  w65c22_->write(W65C22::kAuxiliaryControlRegister, 0);
  w65c22_->write(W65C22::kTimer1LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer1CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            W65C22::kIrqTimer1);
  // Clear timer 1 flag by reading low counter
  w65c22_->read(W65C22::kTimer1CounterLow);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            0);

  // Cross 0 again after overflowing
  for (int i = 0; i < 100000; ++i) {
    w65c22_->tick();
  }
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer1,
            0);
}

TEST_F(W65C22Test, Timer2CounterSetOnlyByHighCounterWrite) {
  uint8_t counter_low = w65c22_->read(W65C22::kTimer2CounterLow);
  uint8_t counter_high = w65c22_->read(W65C22::kTimer2CounterHigh);

  w65c22_->write(W65C22::kTimer2LatchLow, counter_low + 1);
  // Timer 2 low latch write should not affect the counter.
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterLow), counter_low);

  // A high counter write should now move the latches to the counter.
  w65c22_->write(W65C22::kTimer2CounterHigh, counter_high + 2);
  w65c22_->tick();

  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterLow), counter_low + 1);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterHigh), counter_high + 2);
}

TEST_F(W65C22Test, Timer2TickReducesCounterValue) {
  w65c22_->write(W65C22::kTimer2LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer2CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterLow), 0x00);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterHigh), 0x00);
  w65c22_->tick();
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterLow), 0xff);
  EXPECT_EQ(w65c22_->read(W65C22::kTimer2CounterHigh), 0xff);
}

TEST_F(W65C22Test, Timer2InterruptFlagSetWhenCounterReachesZero) {
  w65c22_->write(W65C22::kTimer2LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer2CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            W65C22::kIrqTimer2);
}

TEST_F(W65C22Test, Timer2InterruptFlagClearedWhenLowCounterRead) {
  w65c22_->write(W65C22::kTimer2LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer2CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            W65C22::kIrqTimer2);
  w65c22_->read(W65C22::kTimer2CounterLow);
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            0);
}

TEST_F(W65C22Test, Timer2InterruptFlagClearedWhenHighCounterRead) {
  w65c22_->write(W65C22::kTimer2LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer2CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            W65C22::kIrqTimer2);
  w65c22_->read(W65C22::kTimer2CounterHigh);
  EXPECT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            0);
}

TEST_F(W65C22Test, Timer2DoesNotFireAgainWithoutReset) {
  w65c22_->write(W65C22::kTimer2LatchLow, 0x01);
  w65c22_->write(W65C22::kTimer2CounterHigh, 0x00);
  w65c22_->tick();  // 1
  w65c22_->tick();  // 0
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            W65C22::kIrqTimer2);
  w65c22_->read(W65C22::kTimer2CounterLow);
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            0);

  // Timer 2 should not fire again after the counter reaches 0 again.
  for (int i = 0xffff; i >= 0; --i) {
    w65c22_->tick();
  }
  ASSERT_EQ(w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqTimer2,
            0);
}

TEST_F(W65C22Test, ShiftRegisterWriteInPhi2ModeShiftsOut) {
  // Set up a callback to get the state of the two CB bits. This works because
  // the code is synchronous - but this will need some notifications to be
  // correct if we ever make port callbacks async.
  uint8_t cb_state = 0;
  w65c22_->port_cb()->register_output_change_callback(
      [&cb_state](uint8_t data) { cb_state = data; });

  w65c22_->write(W65C22::kAuxiliaryControlRegister,
                 W65C22::kAcrShiftRegisterOutPhi2);
  w65c22_->write(W65C22::kShiftRegister, 0xa3);
  EXPECT_EQ(w65c22_->read(W65C22::kShiftRegister), 0xa3);
  // The SR interrupt flag should be cleared
  EXPECT_EQ(
      w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqShiftRegister,
      0);
  // No change for 3 cycles after the write.
  for (int i = 0; i < 3; ++i) {
    uint8_t prev_cb_state = cb_state;
    w65c22_->tick();
    EXPECT_EQ(prev_cb_state, cb_state);
  }
  // The value we're shifting out is 0b10100011, starting from MSB. We
  // expect the following states:
  auto expected_states = {
      0b00000010,  // clock down, bit 1
      0b00000011,  // clock up
      0b00000000,  // clock down, bit 0
      0b00000001,  // clock up
      0b00000010,  // clock down, bit 1
      0b00000011,  // clock up
      0b00000000,  // clock down, bit 0
      0b00000001,  // clock up
      0b00000000,  // clock down, bit 0
      0b00000001,  // clock up
      0b00000000,  // clock down, bit 0
      0b00000001,  // clock up
      0b00000010,  // clock down, bit 1
      0b00000011,  // clock up
      0b00000010,  // clock down, bit 1
      0b00000011,  // clock up
  };
  for (auto expected_state : expected_states) {
    w65c22_->tick();
    EXPECT_EQ(cb_state, expected_state);
  }
  // Shifting is done, the SR interrupt flag should be set now.
  EXPECT_EQ(
      w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqShiftRegister,
      W65C22::kIrqShiftRegister);
  // Shifting is done, we don't expect further changes
  for (int i = 0; i < 5; ++i) {
    w65c22_->tick();
    // Clock up, bit 1
    EXPECT_EQ(cb_state, 0b00000011);
  }
}

TEST_F(W65C22Test, ShiftRegisterWriteClearsIFR) {
  w65c22_->write(W65C22::kAuxiliaryControlRegister,
                 W65C22::kAcrShiftRegisterOutPhi2);
  w65c22_->write(W65C22::kShiftRegister, 0xff);
  // The SR interrupt flag should be cleared
  ASSERT_EQ(
      w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqShiftRegister,
      0);
  // 3 setup cycles, 16 to shift the data out
  for (int i = 0; i < 3 + 16; ++i) {
    w65c22_->tick();
  }
  // The SR interrupt flag should be set now.
  ASSERT_EQ(
      w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqShiftRegister,
      W65C22::kIrqShiftRegister);
  // Writing to the SR should clear the IFR
  w65c22_->write(W65C22::kShiftRegister, 0x00);
  EXPECT_EQ(
      w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqShiftRegister,
      0);
}

TEST_F(W65C22Test, PcrValueIsSetAndReturned) {
  w65c22_->write(W65C22::kPeripheralControlRegister, 0x42);
  EXPECT_EQ(w65c22_->read(W65C22::kPeripheralControlRegister), 0x42);
}

TEST_F(W65C22Test, PcrCA2BitsSetCA2HighAndLow) {
  // Set up a callback to get the state of the two CA bits. This works because
  // the code is synchronous - but this will need some notifications to be
  // correct if we ever make port callbacks async.
  uint8_t ca_state = 0;
  w65c22_->port_ca()->register_output_change_callback(
      [&ca_state](uint8_t data) { ca_state = data; });

  w65c22_->write(W65C22::kPeripheralControlRegister, W65C22::kPcrCA2High);
  EXPECT_EQ(ca_state, W65C22::kCa2Mask);
  w65c22_->write(W65C22::kPeripheralControlRegister, W65C22::kPcrCA2Low);
  EXPECT_EQ(ca_state, 0);
}

TEST_F(W65C22Test, CA1FallingTransitionSetsInterruptFlag) {
  // Ensure that CA1 is set to falling-edge transitions (PCR bit 0 is 0)
  w65c22_->write(W65C22::kPeripheralControlRegister,
                 W65C22::kPcrCA1FallingSentive);

  auto ca1_port = w65c22_->port_ca();
  // Ensure CA1 is seeing a high signal
  ca1_port->provide_inputs(W65C22::kCa1Mask, W65C22::kCa1Mask);
  // Clear anything that might have happened as a result of the above
  ClearAllFlags();

  // Falling edge
  ca1_port->provide_inputs(0, W65C22::kCa1Mask);
  EXPECT_EQ(W65C22::kIrqCA1,
            w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
}

TEST_F(W65C22Test, CA1RisingTransitionSetsInterruptFlag) {
  // Ensure that CA1 is set to rising-edge transitions (PCR bit 0 is 1)
  w65c22_->write(W65C22::kPeripheralControlRegister,
                 W65C22::kPcrCA1RisingSentive);

  auto ca1_port = w65c22_->port_ca();
  // Ensure CA1 is seeing a high signal
  ca1_port->provide_inputs(W65C22::kCa1Mask, W65C22::kCa1Mask);
  // Clear anything that might have happened as a result of the above
  ClearAllFlags();

  // Falling edge - expect no flag changes to happen.
  ca1_port->provide_inputs(0, W65C22::kCa1Mask);
  EXPECT_EQ(0, w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);

  // Rising edge - expect the flag to be set.
  ca1_port->provide_inputs(W65C22::kCa1Mask, W65C22::kCa1Mask);
  EXPECT_EQ(W65C22::kIrqCA1,
            w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
}

TEST_F(W65C22Test, PortAReadClearsCA1Flag) {
  // Ensure that CA1 is set to falling-edge transitions (PCR bit 0 is 0)
  w65c22_->write(W65C22::kPeripheralControlRegister,
                 W65C22::kPcrCA1FallingSentive);

  auto ca1_port = w65c22_->port_ca();
  // Ensure CA1 is seeing a high signal
  ca1_port->provide_inputs(W65C22::kCa1Mask, W65C22::kCa1Mask);
  // Clear anything that might have happened as a result of the above
  ClearAllFlags();

  // Falling edge
  ca1_port->provide_inputs(0, W65C22::kCa1Mask);
  EXPECT_EQ(W65C22::kIrqCA1,
            w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
  // Reading the port A register should clear the CA1 flag
  w65c22_->read(W65C22::kOutputRegisterA);
  EXPECT_EQ(0, w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
}

TEST_F(W65C22Test, PortAWriteClearsCA1Flag) {
  // Ensure that CA1 is set to falling-edge transitions (PCR bit 0 is 0)
  w65c22_->write(W65C22::kPeripheralControlRegister,
                 W65C22::kPcrCA1FallingSentive);

  auto ca1_port = w65c22_->port_ca();
  // Ensure CA1 is seeing a high signal
  ca1_port->provide_inputs(W65C22::kCa1Mask, W65C22::kCa1Mask);
  // Clear anything that might have happened as a result of the above
  ClearAllFlags();

  // Falling edge
  ca1_port->provide_inputs(0, W65C22::kCa1Mask);
  EXPECT_EQ(W65C22::kIrqCA1,
            w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
  // Writing to the port A register should clear the CA1 flag
  w65c22_->write(W65C22::kOutputRegisterA, 0);
  EXPECT_EQ(0, w65c22_->read(W65C22::kInterruptFlagRegister) & W65C22::kIrqCA1);
}

}  // namespace
}  // namespace eight_bit