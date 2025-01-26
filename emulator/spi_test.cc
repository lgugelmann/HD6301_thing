#include "spi.h"

#include <gtest/gtest.h>

namespace eight_bit {
namespace {

class SPITest : public ::testing::Test {
 protected:
  static const uint8_t kCsPin = 0;
  static const uint8_t kClkPin = 0;
  static const uint8_t kMosiPin = 0;
  static const uint8_t kMisoPin = 0;

  void SetUp() override {
    cs_port_ = std::make_unique<IOPort>("CS");
    clk_port_ = std::make_unique<IOPort>("CLK");
    mosi_port_ = std::make_unique<IOPort>("MOSI");
    miso_port_ = std::make_unique<IOPort>("MISO");

    cs_port_->write_data_direction_register(1 << kCsPin);
    clk_port_->write_data_direction_register(1 << kClkPin);
    mosi_port_->write_data_direction_register(1 << kMosiPin);
    miso_port_->write_data_direction_register(1 << kMisoPin);

    // CS is normally high.
    cs_port_->write_output_register(kCsPin);
    // CLK is normally low.
    clk_port_->write_output_register(0);

    auto spi_or =
        SPI::create(cs_port_.get(), kCsPin, clk_port_.get(), kClkPin,
                    mosi_port_.get(), kMosiPin, miso_port_.get(), kMisoPin);
    ASSERT_TRUE(spi_or.ok());
    spi_ = std::move(spi_or.value());
  }

  std::unique_ptr<IOPort> cs_port_;
  std::unique_ptr<IOPort> clk_port_;
  std::unique_ptr<IOPort> mosi_port_;
  std::unique_ptr<IOPort> miso_port_;
  std::unique_ptr<SPI> spi_;
};

TEST_F(SPITest, ChipSelectCallbackCalledOnCSLow) {
  bool cs_enabled = false;
  spi_->set_chip_select_callback([&cs_enabled](bool enabled) {
    cs_enabled = enabled;
    return 0;
  });

  cs_port_->write_output_register(0);
  EXPECT_TRUE(cs_enabled);
}

TEST_F(SPITest, ByteReceivedCallbackCalledOnEightShifts) {
  uint8_t byte_received = false;
  spi_->set_byte_received_callback([&byte_received](uint8_t data) {
    byte_received = data;
    return 0;
  });

  cs_port_->write_output_register(0);  // start the transaction
  uint8_t mosi_data = 0xc5;
  for (int i = 0; i < 8; ++i) {
    // SPI is MSBit first.
    mosi_port_->write_output_register(((mosi_data & 0x80) > 0) << kMosiPin);
    mosi_data <<= 1;
    clk_port_->write_output_register(1);
    clk_port_->write_output_register(0);
  }
  EXPECT_EQ(byte_received, 0xc5);
}

}  // namespace
}  // namespace eight_bit
