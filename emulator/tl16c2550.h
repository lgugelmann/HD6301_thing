#ifndef EIGHT_BIT_TL16C2550_H
#define EIGHT_BIT_TL16C2550_H

#include <absl/base/thread_annotations.h>
#include <absl/status/statusor.h>
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>

#include <cstdint>
#include <memory>
#include <queue>
#include <thread>

#include "address_space.h"
#include "interrupt.h"

namespace eight_bit {

class TL16C2550 {
 public:
  TL16C2550(const TL16C2550&) = delete;
  TL16C2550& operator=(const TL16C2550&) = delete;
  ~TL16C2550();

  static absl::StatusOr<std::unique_ptr<TL16C2550>> create(
      AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt);

  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t data);

  std::string get_pty_name(int uart_number) const;

 private:
  TL16C2550(AddressSpace* address_space, uint16_t base_address,
            Interrupt* interrupt);
  absl::Status initialize();

  AddressSpace* address_space_;
  uint16_t base_address_;

  absl::Mutex io_mutex_;

  Interrupt* interrupt_;
  int receive_data_available_irq_id_ ABSL_GUARDED_BY(io_mutex_) = 0;
  int transmit_holding_register_empty_irq_id_ = 0;

  // TODO: change into a struct, implement both UARTS in the TL16C2550.
  uint8_t interrupt_enable_register_ ABSL_GUARDED_BY(io_mutex_) = 0;
  uint8_t interrupt_ident_register_ ABSL_GUARDED_BY(io_mutex_) = 1;
  uint8_t fifo_control_register_ = 0;
  uint8_t line_control_register_ = 0;
  uint8_t modem_control_register_ = 0;
  uint8_t line_status_register_ = 0b01100000;
  uint8_t modem_status_register_ = 0;
  uint8_t scratch_register_ = 0;
  uint8_t divisor_latch_lsb_ = 0;
  uint8_t divisor_latch_msb_ = 0;

  // FD 0 is stdin, so it's usable here as a sentinel value for "not open".
  int our_fd_ = 0;
  int their_fd_ = 0;

  absl::Notification stop_running_;
  std::thread read_thread_;

  std::queue<uint8_t> rx_fifo_ ABSL_GUARDED_BY(io_mutex_);
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_TL16C2550_H