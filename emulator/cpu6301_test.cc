#include "cpu6301.h"

#include "absl/status/status_matchers.h"
#include "gtest/gtest.h"

namespace eight_bit {
namespace {

using ::absl_testing::IsOk;

// A constant for where the tests are set up to start.
uint16_t kProgramStart = 0x0020;
uint16_t kStackTop = 0xf000;

class Cpu6301Test : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a memory space with 64KB.
    memory_ = std::make_unique<AddressSpace>();
    cpu_ = Cpu6301::create(memory_.get()).value();

    // We skip the first 32 bytes as they are CPU-internal and the CPU itself
    // registers read/writes to them. We also avoid the reset vectors so we can
    // trap any read to them to catch illegal instructions.
    ASSERT_THAT(memory_->register_read(
                    0x0020, 0xff00,
                    [this](uint16_t address) { return test_memory_[address]; }),
                IsOk());
    ASSERT_THAT(memory_->register_read(0xfff0, 0xffff,
                                       [this](uint16_t address) {
                                         ADD_FAILURE()
                                             << "Read from reset vectors at "
                                             << std::hex << address
                                             << " should not happen in tests.";
                                         return test_memory_[address];
                                       }),
                IsOk());

    Cpu6301::CpuState initial_state = {
        .a = 0x00,
        .b = 0x00,
        .x = 0x0000,
        .sp = kStackTop,
        .pc = kProgramStart,
        .sr = Cpu6301::StatusRegister(0).as_integer(),
        .breakpoint = std::nullopt,
    };
    cpu_->set_state(initial_state);
  }

  void fail_test_on_memory_write() {
    ASSERT_THAT(memory_->register_write(
                    0x0020, 0xffff,
                    [this](uint16_t address, uint8_t data) {
                      ADD_FAILURE()
                          << "Unexpected write to memory at " << std::hex
                          << address << " with data " << std::hex << (int)data;
                    }),
                IsOk());
  }

  std::unique_ptr<AddressSpace> memory_;
  std::unique_ptr<Cpu6301> cpu_;
  std::array<uint8_t, 65536> test_memory_ = {0};
};

TEST_F(Cpu6301Test, NOP_only_increases_pc) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x01;  // NOP instruction

  Cpu6301::CpuState initial_state = cpu_->get_state();
  // Runs at least one instruction, regardless of how many ticks are requested.
  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;  // NOP should only increment the PC

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, LSRD_ShiftsRight) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x04;  // LSRD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x80;
  initial_state.b = 0x02;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x40;
  expected_state.b = 0x01;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, LSRD_SetsCarryAndZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x04;  // LSRD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x00;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  expected_state.b = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.C = 1;
  sr.Z = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, LSRD_ShiftWithCarryAndNonZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x04;  // LSRD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x01;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  expected_state.b = 0x80;
  Cpu6301::StatusRegister sr(0);
  sr.C = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ASLD_ShiftsLeft) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x05;  // ASLD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x40;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x80;
  expected_state.b = 0x02;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ASLD_SetsCarryAndZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x05;  // ASLD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x80;
  initial_state.b = 0x00;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  expected_state.b = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.C = 1;
  sr.Z = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ASLD_SetsNegative) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x05;  // ASLD

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x40;
  initial_state.b = 0x00;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x80;
  expected_state.b = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TAP_TransfersAToStatusRegister) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x06;  // TAP

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0b00101010;  // H=1, N=1, V=1
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.H = 1;
  sr.N = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TPA_TransfersStatusRegisterToA) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x07;  // TPA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  Cpu6301::StatusRegister sr(0);
  sr.H = 1;
  sr.N = 1;
  sr.V = 1;
  initial_state.sr = sr.as_integer();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  // TPA sets the top two bits of A to 1.
  expected_state.a = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, INX_IncrementsX) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x08;  // INX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0x1234;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.x = 0x1235;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, INX_RollsOverAndSetsZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x08;  // INX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0xffff;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.x = 0x0000;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

}  // namespace
}  // namespace eight_bit