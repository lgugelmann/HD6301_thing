#ifndef EIGHT_BIT_MCP7940N_H
#define EIGHT_BIT_MCP7940N_H

#include <cstdint>
#include <memory>

#include "ioport.h"

namespace eight_bit {

// Emulates a MCP7940N I2C real-time clock chip. Decodes the I2C bit-bang
// protocol from W65C22 Port A where SCL is bit 0 and SDA is bit 1. Time
// registers always reflect the host system clock; writes to control registers
// are accepted but otherwise ignored.
class MCP7940N {
 public:
  MCP7940N(const MCP7940N&) = delete;
  MCP7940N& operator=(const MCP7940N&) = delete;

  static std::unique_ptr<MCP7940N> create(IOPort* port_a);

 private:
  explicit MCP7940N(IOPort* port_a);

  static constexpr uint8_t kSCLMask = 0x01;  // Port A bit 0
  static constexpr uint8_t kSDAMask = 0x02;  // Port A bit 1
  static constexpr uint8_t kDeviceAddress = 0x6F;

  enum class State {
    kIdle,
    kReceivingAddr,
    kAcking,
    kReceivingReg,
    kReceivingData,
    kSendingData,
    kMasterAcking,
  };

  void on_ddr_change(uint8_t ddr);
  void on_scl_rising();
  void on_scl_falling();
  void on_start();
  void on_stop();

  uint8_t read_register(uint8_t reg) const;
  void write_register(uint8_t reg, uint8_t value);
  void drive_sda_bit();

  IOPort* port_a_;

  // Current logical levels of the I2C lines as driven by the master (derived
  // from the DDR: DDR bit=1 means driven low, DDR bit=0 means floating high).
  bool scl_ = true;
  bool sda_ = true;

  State state_ = State::kIdle;
  // Saves the state we were in before entering kAcking.
  State acking_prev_state_ = State::kIdle;

  // Number of bits received or sent in the current byte.
  int bit_count_ = 0;
  // Byte being assembled (receive) or transmitted (send).
  uint8_t current_byte_ = 0;
  uint8_t send_byte_ = 0;
  // Current register pointer, incremented automatically after each byte.
  uint8_t reg_pointer_ = 0;
  // True if the current transaction is a read (master reads from slave).
  bool is_read_ = false;

  // Register file: 0x00–0x08 are time/control registers; 0x20–0x5F are SRAM.
  uint8_t registers_[0x60] = {};
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_MCP7940N_H
