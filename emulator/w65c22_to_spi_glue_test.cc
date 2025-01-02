#include "w65c22_to_spi_glue.h"

#include <gtest/gtest.h>

namespace eight_bit {
namespace {

class W65C22ToSPIGlueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clk_in_ = std::make_unique<IOPort>("CLK in");
    clk_in_->set_direction(1);
    parallel_out_ = std::make_unique<IOPort>("Parallel out");
    // The "parallel out" is actually an input to the W65C22
    parallel_out_->set_direction(0);
    w65c22_to_spi_glue_ =
        W65C22ToSPIGlue::create(clk_in_.get(), 0, parallel_out_.get()).value();
  }

  std::unique_ptr<W65C22ToSPIGlue> w65c22_to_spi_glue_;
  std::unique_ptr<IOPort> clk_in_;
  std::unique_ptr<IOPort> parallel_out_;
};

TEST_F(W65C22ToSPIGlueTest, MisoBitInWritesToParallelOut) {
  ASSERT_EQ(parallel_out_->read(), 0);

  IOPort* miso_port = w65c22_to_spi_glue_->miso_port();
  miso_port->write(0);                              // 0
  miso_port->write(W65C22ToSPIGlue::kMisoBitmask);  // 01
  EXPECT_EQ(parallel_out_->read(), 0);              // Not got to 8 bits yet
  miso_port->write(W65C22ToSPIGlue::kMisoBitmask);  // 011
  miso_port->write(0);                              // 0110
  miso_port->write(W65C22ToSPIGlue::kMisoBitmask);  // 01101
  miso_port->write(0);                              // 011010
  miso_port->write(W65C22ToSPIGlue::kMisoBitmask);  // 0110101
  EXPECT_EQ(parallel_out_->read(), 0);              // No write yet
  miso_port->write(0);                              // 01101010
  // 8 bits written, should be written to parallel_out_
  EXPECT_EQ(parallel_out_->read(), 0b01101010);
}

TEST_F(W65C22ToSPIGlueTest, ClkBitInWritesInvertedToClkOut) {
  IOPort* clk_out = w65c22_to_spi_glue_->clk_out_port();
  uint8_t clk_out_data = 0;
  clk_out->register_write_callback(
      [&clk_out_data](uint8_t data) { clk_out_data = data; });

  clk_in_->write(0);
  w65c22_to_spi_glue_->tick();
  EXPECT_EQ(clk_out_data, 1);  // inverted 0
  clk_in_->write(1);
  EXPECT_EQ(clk_out_data, 1);  // still 1, new clk not yet presented
  w65c22_to_spi_glue_->tick();
  EXPECT_EQ(clk_out_data, 0);  // inverted 1
}

}  // namespace
}  // namespace eight_bit