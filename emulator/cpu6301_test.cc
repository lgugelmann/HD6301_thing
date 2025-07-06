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

TEST_F(Cpu6301Test, DEX_DecrementsX) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x09;  // DEX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0x1234;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.x = 0x1233;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, DEX_RollsOver) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x09;  // DEX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0x0000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.x = 0xffff;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, DEX_SetsZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x09;  // DEX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0x0001;
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

TEST_F(Cpu6301Test, CLV_ClearsOverflow) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0A;  // CLV

  Cpu6301::CpuState initial_state = cpu_->get_state();
  Cpu6301::StatusRegister sr(0);
  sr.V = 1;
  initial_state.sr = sr.as_integer();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  sr.V = 0;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SEV_SetsOverflow) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0B;  // SEV

  Cpu6301::CpuState initial_state = cpu_->get_state();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, CLC_ClearsCarry) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0C;  // CLC

  Cpu6301::CpuState initial_state = cpu_->get_state();
  Cpu6301::StatusRegister sr(0);
  sr.C = 1;
  initial_state.sr = sr.as_integer();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  sr.C = 0;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SEC_SetsCarry) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0D;  // SEC

  Cpu6301::CpuState initial_state = cpu_->get_state();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.C = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, CLI_ClearsInterruptMask) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0E;  // CLI

  Cpu6301::CpuState initial_state = cpu_->get_state();
  Cpu6301::StatusRegister sr(0);
  sr.I = 1;
  initial_state.sr = sr.as_integer();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  sr.I = 0;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SEI_SetsInterruptMask) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x0F;  // SEI

  Cpu6301::CpuState initial_state = cpu_->get_state();
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.I = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SBA_SubtractsBFromA) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x10;  // SBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x0f;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SBA_SetsZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x10;  // SBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x10;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SBA_SetsNegative) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x10;  // SBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x20;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0xf0;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  sr.C = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, CBA_ComparesAccumulators) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x11;  // CBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, CBA_SetsZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x11;  // CBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x10;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, CBA_SetsNegative) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x11;  // CBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x20;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  sr.C = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ABA_AddsBToA) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x1B;  // ABA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x01;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x11;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ABA_SetsZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x1B;  // ABA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x00;
  initial_state.b = 0x00;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ABA_SetsNegative) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x1B;  // ABA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x80;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x90;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ABA_SetsCarry) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x1B;  // ABA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x80;
  initial_state.b = 0x80;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  sr.C = 1;
  sr.V = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TSX_TransfersSPPlusOneToX) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x30;  // TSX

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.x = 0x1001;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, INS_IncrementsSP) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x31;  // INS

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = 0x1001;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, INS_RollsOver) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x31;  // INS

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = 0xffff;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = 0x0000;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}



TEST_F(Cpu6301Test, DES_DecrementsSP) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x34;  // DES

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = 0x0fff;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, DES_RollsOver) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x34;  // DES

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = 0x0000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = 0xffff;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TXS_TransfersXMinusOneToSP) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x35;  // TXS

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.x = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = 0x0fff;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, PULA_PullsValueFromStack) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x32;  // PULA
  test_memory_[kStackTop] = 0x42;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = kStackTop - 1;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = kStackTop;
  expected_state.a = 0x42;
  // PULA should not affect the status register.

  EXPECT_EQ(result.cycles_run, 3);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, PULB_PullsValueFromStack) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x33;  // PULB
  test_memory_[kStackTop] = 0x42;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.sp = kStackTop - 1;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp = kStackTop;
  expected_state.b = 0x42;
  // PULB should not affect the status register.

  EXPECT_EQ(result.cycles_run, 3);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, PSHA_PushesValueToStack) {
  ASSERT_THAT(memory_->register_write(
                  kStackTop, kStackTop,
                  [this](uint16_t address, uint8_t data) {
                    test_memory_[address] = data;
                  }),
              IsOk());
  test_memory_[kProgramStart] = 0x36;  // PSHA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x42;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp -= 1;
  // PSHA should not affect the status register.

  EXPECT_EQ(result.cycles_run, 4);
  EXPECT_EQ(final_state, expected_state);
  EXPECT_EQ(test_memory_[kStackTop], 0x42);
}

TEST_F(Cpu6301Test, PSHB_PushesValueToStack) {
  ASSERT_THAT(memory_->register_write(
                  kStackTop, kStackTop,
                  [this](uint16_t address, uint8_t data) {
                    test_memory_[address] = data;
                  }),
              IsOk());
  test_memory_[kProgramStart] = 0x37;  // PSHB

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.b = 0x42;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.sp -= 1;
  // PSHB should not affect the status register.

  EXPECT_EQ(result.cycles_run, 4);
  EXPECT_EQ(final_state, expected_state);
  EXPECT_EQ(test_memory_[kStackTop], 0x42);
}

TEST_F(Cpu6301Test, TAB_TransfersAtoB) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x16;  // TAB

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x42;
  initial_state.b = 0x11;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.b = 0x42;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TAB_TransfersNegativeValue) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x16;  // TAB

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x80;
  initial_state.b = 0x11;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.b = 0x80;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TAB_TransfersZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x16;  // TAB

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x00;
  initial_state.b = 0x11;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.b = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TBA_TransfersBtoA) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x17;  // TBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x11;
  initial_state.b = 0x42;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x42;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TBA_TransfersNegativeValue) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x17;  // TBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x11;
  initial_state.b = 0x80;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x80;
  Cpu6301::StatusRegister sr(0);
  sr.N = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, TBA_TransfersZero) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x17;  // TBA

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x11;
  initial_state.b = 0x00;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 1;
  expected_state.a = 0x00;
  Cpu6301::StatusRegister sr(0);
  sr.Z = 1;
  expected_state.sr = sr.as_integer();

  EXPECT_EQ(result.cycles_run, 1);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ADDD_Immediate) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xC3;  // ADDD #data16
  test_memory_[kProgramStart + 1] = 0x12;
  test_memory_[kProgramStart + 2] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x20;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 3;
  expected_state.a = 0x22;
  expected_state.b = 0x54;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 3);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ADDD_Direct) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xD3;  // ADDD addr8
  test_memory_[kProgramStart + 1] = 0x42;
  test_memory_[0x42] = 0x12;
  test_memory_[0x43] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x20;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 2;
  expected_state.a = 0x22;
  expected_state.b = 0x54;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 4);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ADDD_Indexed) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xE3;  // ADDD Disp,X
  test_memory_[kProgramStart + 1] = 0x10;
  test_memory_[0x1010] = 0x12;
  test_memory_[0x1011] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x10;
  initial_state.b = 0x20;
  initial_state.x = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 2;
  expected_state.a = 0x22;
  expected_state.b = 0x54;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 5);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, ADDD_Extended) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xF3;  // ADDD addr16
  test_memory_[kProgramStart + 1] = 0x12;
  test_memory_[kProgramStart + 2] = 0x34;
  test_memory_[0x1234] = 0x10;
  test_memory_[0x1235] = 0x20;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x30;
  initial_state.b = 0x40;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 3;
  expected_state.a = 0x40;
  expected_state.b = 0x60;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 5);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SUBD_Immediate) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x83;  // SUBD #data16
  test_memory_[kProgramStart + 1] = 0x12;
  test_memory_[kProgramStart + 2] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x56;
  initial_state.b = 0x78;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 3;
  expected_state.a = 0x44;
  expected_state.b = 0x44;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 3);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SUBD_Direct) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0x93;  // SUBD addr8
  test_memory_[kProgramStart + 1] = 0x42;
  test_memory_[0x42] = 0x12;
  test_memory_[0x43] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x56;
  initial_state.b = 0x78;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 2;
  expected_state.a = 0x44;
  expected_state.b = 0x44;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 4);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SUBD_Indexed) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xA3;  // SUBD Disp,X
  test_memory_[kProgramStart + 1] = 0x10;
  test_memory_[0x1010] = 0x12;
  test_memory_[0x1011] = 0x34;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x56;
  initial_state.b = 0x78;
  initial_state.x = 0x1000;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 2;
  expected_state.a = 0x44;
  expected_state.b = 0x44;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 5);
  EXPECT_EQ(final_state, expected_state);
}

TEST_F(Cpu6301Test, SUBD_Extended) {
  fail_test_on_memory_write();
  test_memory_[kProgramStart] = 0xB3;  // SUBD addr16
  test_memory_[kProgramStart + 1] = 0x12;
  test_memory_[kProgramStart + 2] = 0x34;
  test_memory_[0x1234] = 0x10;
  test_memory_[0x1235] = 0x20;

  Cpu6301::CpuState initial_state = cpu_->get_state();
  initial_state.a = 0x50;
  initial_state.b = 0x60;
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();

  Cpu6301::CpuState expected_state = initial_state;
  expected_state.pc += 3;
  expected_state.a = 0x40;
  expected_state.b = 0x40;
  expected_state.sr = Cpu6301::StatusRegister(0).as_integer();

  EXPECT_EQ(result.cycles_run, 5);
  EXPECT_EQ(final_state, expected_state);
}

}  // namespace
}  // namespace eight_bit