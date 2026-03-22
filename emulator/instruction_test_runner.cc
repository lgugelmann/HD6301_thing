#include "instruction_test_parser.h"

#include <array>
#include <iomanip>
#include <memory>
#include <sstream>

#include "absl/status/status_matchers.h"
#include "address_space.h"
#include "cpu6301.h"
#include "gtest/gtest.h"

namespace eight_bit {
namespace {

using ::absl_testing::IsOk;

class InstructionTest
    : public ::testing::TestWithParam<InstructionTestCase> {
 protected:
  void SetUp() override {
    memory_ = std::make_unique<AddressSpace>();
    cpu_ = Cpu6301::create(memory_.get()).value();

    ASSERT_THAT(
        memory_->register_read(
            0x0020, 0xff00,
            [this](uint16_t address) { return test_memory_[address]; }),
        IsOk());
    ASSERT_THAT(
        memory_->register_read(
            0xfff0, 0xffff,
            [this](uint16_t address) { return test_memory_[address]; }),
        IsOk());
    ASSERT_THAT(
        memory_->register_write(
            0x0020, 0xffff,
            [this](uint16_t address, uint8_t data) {
              test_memory_[address] = data;
            }),
        IsOk());
  }

  std::unique_ptr<AddressSpace> memory_;
  std::unique_ptr<Cpu6301> cpu_;
  std::array<uint8_t, 65536> test_memory_ = {0};
};

TEST_P(InstructionTest, Execute) {
  const auto& tc = GetParam();

  // Place instruction bytes at PC.
  for (size_t i = 0; i < tc.memory.size(); ++i) {
    test_memory_[tc.pc + i] = tc.memory[i];
  }

  // Pre-load memory regions.
  for (const auto& [addr, bytes] : tc.memory_at) {
    for (size_t i = 0; i < bytes.size(); ++i) {
      test_memory_[addr + i] = bytes[i];
    }
  }

  // Set initial CPU state.
  Cpu6301::CpuState initial_state = {
      .a = tc.a,
      .b = tc.b,
      .x = tc.x,
      .sp = tc.sp,
      .pc = tc.pc,
      .sr = tc.sr,
      .breakpoint = std::nullopt,
  };
  cpu_->set_state(initial_state);

  auto result = cpu_->tick(1);
  Cpu6301::CpuState final_state = cpu_->get_state();
  Cpu6301::StatusRegister final_sr(final_state.sr);

  // Check expected registers.
  if (tc.expect_a)
    EXPECT_EQ(final_state.a, *tc.expect_a) << "register A";
  if (tc.expect_b)
    EXPECT_EQ(final_state.b, *tc.expect_b) << "register B";
  if (tc.expect_x)
    EXPECT_EQ(final_state.x, *tc.expect_x) << "register X";
  if (tc.expect_sp)
    EXPECT_EQ(final_state.sp, *tc.expect_sp) << "register SP";
  if (tc.expect_pc)
    EXPECT_EQ(final_state.pc, *tc.expect_pc) << "register PC";
  if (tc.expect_sr)
    EXPECT_EQ(final_state.sr, *tc.expect_sr) << "status register";

  // Check expected flags.
  if (tc.expect_H) EXPECT_EQ(final_sr.H, *tc.expect_H) << "flag H";
  if (tc.expect_I) EXPECT_EQ(final_sr.I, *tc.expect_I) << "flag I";
  if (tc.expect_N) EXPECT_EQ(final_sr.N, *tc.expect_N) << "flag N";
  if (tc.expect_Z) EXPECT_EQ(final_sr.Z, *tc.expect_Z) << "flag Z";
  if (tc.expect_V) EXPECT_EQ(final_sr.V, *tc.expect_V) << "flag V";
  if (tc.expect_C) EXPECT_EQ(final_sr.C, *tc.expect_C) << "flag C";

  // Check expected memory.
  for (const auto& [addr, expected_byte] : tc.expect_at) {
    EXPECT_EQ(test_memory_[addr], expected_byte)
        << "memory at 0x" << std::hex << std::setfill('0') << std::setw(4)
        << addr;
  }

  // Check cycle count.
  if (tc.cycles) {
    EXPECT_EQ(result.cycles_run, *tc.cycles) << "cycle count";
  }
}

std::string TestName(const ::testing::TestParamInfo<InstructionTestCase>& info) {
  const auto& tc = info.param;
  // Build a name from filename (without extension) + test name.
  std::string file_stem = std::filesystem::path(tc.file).stem().string();
  std::string name = file_stem + "_" + tc.name;
  // Replace non-alphanumeric chars with underscores for GoogleTest compatibility.
  for (char& c : name) {
    if (!std::isalnum(c)) c = '_';
  }
  return name;
}

auto LoadTestCases() {
  std::filesystem::path dir(TESTDATA_DIR "/instructions");
  auto result = ParseAllTestFiles(dir);
  if (!result.error.empty()) {
    // Can't use FAIL() here, but we can print and return empty.
    std::cerr << "ERROR loading test files: " << result.error << std::endl;
    return std::vector<InstructionTestCase>{};
  }
  return std::move(result.cases);
}

INSTANTIATE_TEST_SUITE_P(Instructions, InstructionTest,
                         ::testing::ValuesIn(LoadTestCases()), TestName);

}  // namespace
}  // namespace eight_bit
