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


}  // namespace
}  // namespace eight_bit
