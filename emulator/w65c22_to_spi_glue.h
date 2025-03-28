#ifndef EIGHT_BIT_W65C22_TO_SPI_GLUE_H
#define EIGHT_BIT_W65C22_TO_SPI_GLUE_H

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "ioport.h"

namespace eight_bit {

// This class implements the hardware logic to convert the W65C22's shift
// register output to SPI Mode 0 MOSI, and to implement the serial-to-parallel
// conversion for MISO as well. That involves inverting and delaying the clock
// in one direction, and acting as a shift register for the MISO bit in the
// other. In addition it also provides the keyboard emulation and multiplexing
// for the keyboard input that's also connected to the same W65C22 port.
class W65C22ToSPIGlue {
 public:
  W65C22ToSPIGlue(const W65C22ToSPIGlue&) = delete;
  ~W65C22ToSPIGlue() = default;

  static absl::StatusOr<std::unique_ptr<W65C22ToSPIGlue>> create(
      IOPort* clk_in_port, uint8_t clk_in_pin, IOPort* output_switch_port,
      uint8_t output_switch_pin, IOPort* keyboard_irq_port,
      uint8_t keyboard_irq_pin, IOPort* parallel_out_port);

  // Tick callback to simulate clock delay.
  void tick();

  // Handle the keyboard event. On actual hardware this is an SLG46826 that acts
  // as a shift register for PS2 data. Once it receives a full byte it sends a
  // pulse to CA1 on the 65C22. The actual byte is multiplexed on
  // 'parallel_out_port_' together with the SD card data. The
  // 'output_switch_port_' state is used to select between the two.
  void handle_keyboard_event(SDL_KeyboardEvent event);

  IOPort* clk_out_port();
  IOPort* miso_port();

  static const uint8_t kMisoPin = 0;
  static const uint8_t kMisoBitmask = 1 << kMisoPin;
  static const uint8_t kClkPin = 0;
  static const uint8_t kClkBitmask = 1 << kClkPin;

  // PS2 data rate is 7–12 kbit/second. Let's call that 1 kbyte/second for
  // simplicity. That's 1000 bytes per second, or 1 byte every 1ms.
  static const int kKeyboardByteTickInterval = 1000;

 private:
  W65C22ToSPIGlue(IOPort* clk_in_port, uint8_t clk_in_pin,
                  IOPort* output_switch_port, uint8_t output_switch_pin,
                  IOPort* keyboard_irq_port, uint8_t keyboard_irq_pin,
                  IOPort* parallel_out_port);

  // Callback for the clock input port. Writes the inverted bit to
  // clk_out_port_.
  void clk_bit_in(uint8_t data);
  // Callback for the MISO input port. Shifts in the bits and presents them to
  // parallel_out_port_ once 8 bits are received.
  void miso_bit_in(uint8_t data);

  IOPort clk_out_port_;
  IOPort miso_port_;
  IOPort* clk_in_port_ = nullptr;
  const uint8_t clk_in_mask_ = 0;
  IOPort* output_switch_port_ = nullptr;
  const uint8_t output_switch_mask_ = 0;
  IOPort* keyboard_irq_port_ = nullptr;
  const uint8_t keyboard_irq_mask_ = 0;
  IOPort* parallel_out_port_ = nullptr;

  uint8_t miso_shift_data_ = 0;
  int shift_count_ = 0;
  // True if the clock changed during the last tick
  bool clock_on_last_tick_ = false;
  // The value of the clock on the last tick
  uint8_t prev_clock_ = 0;
  // A counter for the number of ticks to wait between providing keyboard bytes.
  // Roughly corresponds to PS2 data rate.
  int keyboard_tick_countdown_ = 0;

  // The data from the SD card or keyboard. To be output on the
  // parallel_out_port_ depending on the state of the output_switch_port_.
  absl::Mutex mutex_;
  uint8_t sd_card_data_ ABSL_GUARDED_BY(mutex_) = 0;
  std::queue<uint8_t> keyboard_data_queue_ ABSL_GUARDED_BY(mutex_);
  uint8_t keyboard_data_ = 0;
  bool output_keyboard_data_ = false;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_MISO_TO_PARALLEL_H