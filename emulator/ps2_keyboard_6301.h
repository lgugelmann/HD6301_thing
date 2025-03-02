#ifndef EIGHT_BIT_PS2_KEYBOARD_6301_H
#define EIGHT_BIT_PS2_KEYBOARD_6301_H

#include <SDL.h>

#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "interrupt.h"
#include "ioport.h"

namespace eight_bit {

// Emulates a PS/2 keyboard connected to an 8-bit I/O port for parallel data
// communication (some other hardware converts the serial data to parallel) and
// another I/O port for interrupt handling (data present bit, and a clear signal
// for that bit). Thread safe. This represents the setup for the PS2 port on the
// 6301 CPU board, where the data pins are on one of the 8-bit ports and the IRQ
// logic on port 2.
class PS2Keyboard6301 {
 public:
  PS2Keyboard6301(Interrupt* irq, IOPort* data_port, IOPort* irq_status_port);
  ~PS2Keyboard6301() = default;

  void handle_keyboard_event(SDL_KeyboardEvent event);

  const uint8_t kIrqClearPin = 0;
  const uint8_t kIrqClearMask = 1 << kIrqClearPin;
  const uint8_t kIrqStatusPin = 1;
  const uint8_t kIrqStatusMask = 1 << kIrqStatusPin;

 private:
  Interrupt* irq_ = nullptr;
  IOPort* data_port_ = nullptr;
  IOPort* irq_status_port_ = nullptr;

  absl::Mutex mutex_;
  std::queue<uint8_t> data_ ABSL_GUARDED_BY(mutex_);
  uint8_t interrupt_clear_ ABSL_GUARDED_BY(mutex_) = 0;
  int interrupt_id_ ABSL_GUARDED_BY(mutex_) = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_PS2_KEYBOARD_6301_H