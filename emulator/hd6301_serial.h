#ifndef EIGHT_BIT_HD6301_SERIAL_H
#define EIGHT_BIT_HD6301_SERIAL_H

#include <absl/status/statusor.h>
#include <absl/synchronization/mutex.h>

#include <cstdint>
#include <memory>
#include <queue>
#include <thread>

#include "address_space.h"
#include "interrupt.h"

namespace eight_bit {

// HD6301Serial implements parts of the Serial Communications Interface (SCI) of
// the HD6301 processor. It only supports internal clocks and doesn't do any of
// the wakeup features.
class HD6301Serial {
 public:
  HD6301Serial(const HD6301Serial&) = delete;
  HD6301Serial& operator=(const HD6301Serial&) = delete;
  ~HD6301Serial();

  static absl::StatusOr<std::unique_ptr<HD6301Serial>> Create(
      AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt);

  void tick();

  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t data);

 private:
  HD6301Serial(AddressSpace* address_space, uint16_t base_address,
               Interrupt* interrupt);
  absl::Status initialize();

  AddressSpace* address_space_;
  uint16_t base_address_;

  Interrupt* interrupt_;
  int transmit_interrupt_id_ = 0;
  int receive_interrupt_id_ = 0;

  // Transmit/Receive Control Status Register
  uint8_t trcsr_ = 0b00100000;
  // Transfer Rate/Mode Control Register
  uint8_t rmcr_ = 0;
  uint8_t receive_data_register_ = 0;

  uint8_t transmit_register_empty_countdown_ = 0;
  uint8_t receive_register_full_countdown_ = 0;

  // FD 0 is stdin, so it's usable here as a sentinel value for "not open".
  int our_fd_ = 0;
  int their_fd_ = 0;

  std::queue<uint8_t> rx_fifo_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_HD6301_SERIAL_H