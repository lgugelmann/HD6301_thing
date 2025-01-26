#ifndef EIGHT_BIT_SPI_H
#define EIGHT_BIT_SPI_H

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "ioport.h"

namespace eight_bit {

// An adapter class that translates between bit-banged SPI on an IOPort to a
// more convenient byte-based interface. This code acts as a SPI sub. The main
// side is expected to drive the clock as well as cs and mosi. It implements SPI
// mode 0 only.
class SPI {
 public:
  typedef std::function<uint8_t(uint8_t)> byte_received_callback;
  typedef std::function<uint8_t(bool)> chip_select_callback;

  ~SPI() = default;
  SPI(const SPI&) = delete;

  static absl::StatusOr<std::unique_ptr<SPI>> create(
      IOPort* cs_port, uint8_t cs_pin, IOPort* clk_port, uint8_t clk_pin,
      IOPort* mosi_port, uint8_t mosi_pin, IOPort* miso_port, uint8_t miso_pin);

  // Register a callback that's called when a byte is received. The callback
  // returns the byte to send back with the next 8 clock cycles of the current
  // command. If the command is complete, that byte is ignored. If a callback is
  // already set, it will be replaced.
  void set_byte_received_callback(const byte_received_callback& callback);

  // Register a callback that's called when the chip select line changes. The
  // boolean passed to the callback is true when the chip select line indicates
  // 'enabled', false otherwise. The callback returns the first byte to send on
  // the MISO line.
  void set_chip_select_callback(const chip_select_callback& callback);

 private:
  SPI(IOPort* cs_port, uint8_t cs_pin, IOPort* clk_port, uint8_t clk_pin,
      IOPort* mosi_port, uint8_t mosi_pin, IOPort* miso_port, uint8_t miso_pin);

  absl::Status initialize();

  void clk_data_in(uint8_t data);
  void cs_data_in(uint8_t data);
  void sub_data_in(uint8_t data);

  IOPort* cs_port_;
  IOPort* clk_port_;
  IOPort* mosi_port_;
  IOPort* miso_port_;
  uint8_t cs_mask_;
  uint8_t clk_mask_;
  uint8_t mosi_mask_;
  uint8_t miso_mask_;

  byte_received_callback byte_received_callback_;
  chip_select_callback chip_select_callback_;

  struct SPIState {
    // These bytes hold the data that is being shifted out and in from the sub
    // side of the SPI connection.
    uint8_t sub_data_out = 0xff;
    uint8_t sub_data_in = 0;
    // The number of bits shifted in so far. Every 8 bits the
    // byte_received_callback is invoked.
    uint8_t bit_count = 0;
    // The previous clock and chip select values, used to detect edges.
    uint8_t previous_clock = 0;
    uint8_t previous_cs = 0;
    // The data returned on reads of the MISO IO port.
    uint8_t miso_port_data = 0;
    // The current state of the MOSI bit, either 0 or 1.
    uint8_t mosi_bit = 0;
  };
  SPIState state_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_SPI_H