#ifndef EIGHT_BIT_W65C22_TO_SPI_GLUE_H
#define EIGHT_BIT_W65C22_TO_SPI_GLUE_H

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "ioport.h"

namespace eight_bit {

// This class implements the hardware logic to convert the W65C22's shift
// register output to SPI Mode 0 MOSI, and to implement the serial-to-parallel
// conversion for MISO as well. That involves inverting and delaying the clock
// in one direction, and acting as a shift register for the MISO bit in the
// other.
class W65C22ToSPIGlue {
 public:
  W65C22ToSPIGlue(const W65C22ToSPIGlue&) = delete;
  ~W65C22ToSPIGlue() = default;

  static absl::StatusOr<std::unique_ptr<W65C22ToSPIGlue>> create(
      IOPort* clk_in_port, uint8_t clk_in_pin, IOPort* parallel_out_port);

  // Tick callback to simulate clock delay.
  void tick();

  IOPort* clk_out_port();
  IOPort* miso_port();

  static const uint8_t kMisoPin = 0;
  static const uint8_t kMisoBitmask = 1 << kMisoPin;
  static const uint8_t kClkPin = 0;
  static const uint8_t kClkBitmask = 1 << kClkPin;

 private:
  W65C22ToSPIGlue(IOPort* clk_in_port, uint8_t clk_in_pin,
                  IOPort* parallel_out_port);

  // Callback for the clock input port. Writes the inverted bit to
  // clk_out_port_.
  void clk_bit_in(uint8_t data);
  // Callback for the MISO input port. Shifts in the bits and presents them to
  // parallel_out_ once 8 bits are received.
  void miso_bit_in(uint8_t data);
  uint8_t parallel_out_data() const { return parallel_out_data_; }

  IOPort clk_out_port_;
  IOPort miso_port_;
  IOPort* clk_in_port_;
  const uint8_t clk_in_pin_;

  uint8_t parallel_out_data_ = 0;
  uint8_t miso_shift_data_ = 0;
  int shift_count_ = 0;
  // True if the clock changed during the last tick
  bool clock_on_last_tick_ = false;
  // The value of the clock on the last tick
  uint8_t prev_clock_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_MISO_TO_PARALLEL_H