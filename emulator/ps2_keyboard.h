#ifndef EIGHT_BIT_PS2_KEYBOARD_H
#define EIGHT_BIT_PS2_KEYBOARD_H

#include <SDL.h>

#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "interrupt.h"
#include "ioport.h"

namespace eight_bit {

class PS2Keyboard {
 public:
  PS2Keyboard(Interrupt* irq, IOPort* data_port, IOPort* irq_status_port);
  ~PS2Keyboard() = default;

  void handle_keyboard_event(SDL_KeyboardEvent event);

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

#endif  // EIGHT_BIT_PS2_KEYBOARD_H