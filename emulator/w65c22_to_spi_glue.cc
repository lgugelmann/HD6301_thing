#include "w65c22_to_spi_glue.h"

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<W65C22ToSPIGlue>>
eight_bit::W65C22ToSPIGlue::create(IOPort* clk_in_port, uint8_t clk_in_pin,
                                   IOPort* parallel_out_port) {
  auto miso_to_parallel = std::unique_ptr<W65C22ToSPIGlue>(
      new W65C22ToSPIGlue(clk_in_port, clk_in_pin, parallel_out_port));
  return miso_to_parallel;
}

void W65C22ToSPIGlue::tick() {
  if (clock_on_last_tick_) {
    clk_out_port_.write(prev_clock_ << kClkPin);
    clock_on_last_tick_ = false;
    if (prev_clock_ == 1) {
      // Rising edge, sample MISO
      miso_bit_in(miso_port_.read());
    }
  }
}

IOPort* W65C22ToSPIGlue::clk_out_port() { return &clk_out_port_; }

IOPort* W65C22ToSPIGlue::miso_port() { return &miso_port_; }

W65C22ToSPIGlue::W65C22ToSPIGlue(IOPort* clk_in_port, uint8_t clk_in_pin,
                                 IOPort* parallel_out_port)
    : clk_out_port_("W65C22 SPI glue clock out"),
      miso_port_("W65C22 SPI glue miso"),
      clk_in_port_(clk_in_port),
      clk_in_pin_(clk_in_pin) {
  clk_out_port_.set_direction(kClkBitmask);
  miso_port_.set_direction(kMisoBitmask);
  miso_port_.register_write_callback(
      [this](uint8_t data) { miso_bit_in(data); });
  clk_in_port_->register_write_callback(
      [this](uint8_t data) { clk_bit_in(data); });
  parallel_out_port->register_read_callback(
      [this]() { return parallel_out_data(); });
}

void W65C22ToSPIGlue::clk_bit_in(uint8_t data) {
  clock_on_last_tick_ = true;
  prev_clock_ = (data & (1 << clk_in_pin_)) != 1;
}

void W65C22ToSPIGlue::miso_bit_in(uint8_t data) {
  uint8_t bit = (data & kMisoBitmask) == 1;
  miso_shift_data_ = (miso_shift_data_ << 1) | bit;
  ++shift_count_;
  if (shift_count_ == 8) {
    parallel_out_data_ = miso_shift_data_;
    shift_count_ = 0;
  }
}

}  // namespace eight_bit