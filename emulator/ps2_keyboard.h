#ifndef EIGHT_BIT_PS2_KEYBOARD_H
#define EIGHT_BIT_PS2_KEYBOARD_H

#include <SDL2/SDL_events.h>

#include <queue>

#include "interrupt.h"
#include "ioport.h"

namespace eight_bit {

class PS2Keyboard {
 public:
  PS2Keyboard(Interrupt* irq, IOPort* data_port, IOPort* irq_status_port);
  ~PS2Keyboard();

  void handle_keyboard_event(SDL_KeyboardEvent event);

 private:
  Interrupt* irq_ = nullptr;
  IOPort* data_port_ = nullptr;
  IOPort* irq_status_port_ = nullptr;

  std::queue<uint8_t> data_;
  uint8_t interrupt_clear_ = 0;
  int interrupt_id_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_PS2_KEYBOARD_H