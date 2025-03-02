#include "ps2_keyboard_6301.h"

#include <map>
#include <span>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "sdl_to_ps2_keymap.h"

namespace eight_bit {

PS2Keyboard6301::PS2Keyboard6301(Interrupt* irq, IOPort* data_port,
                                 IOPort* irq_status_port)
    : irq_(irq), data_port_(data_port), irq_status_port_(irq_status_port) {
  // Bit 0 on the irq status port can be written to and is used to clear the
  // keyboard interrupt by being pulled low then high again.
  irq_status_port_->register_output_change_callback([this](uint8_t data) {
    absl::MutexLock lock(&mutex_);
    data = data & kIrqClearMask;
    // We saw a 0->1 transition on the interrupt clear bit. Clear the
    // interrupt.
    if (data == kIrqClearMask && interrupt_clear_ == 0) {
      VLOG(1) << "Clearing keyboard interrupt";
      if (interrupt_id_ != 0) {
        irq_->clear_interrupt(interrupt_id_);
        interrupt_id_ = 0;
        irq_status_port_->provide_inputs(kIrqStatusMask, kIrqStatusMask);
      }
      // TODO: This should really happen after some given amount of time
      // corresponding to ps2 data rates, but for now just assume that the
      // interrupt is cleared after reading keyboard data.
      if (!data_.empty()) {
        data_.pop();
      }
      if (!data_.empty()) {
        data_port_->provide_inputs(data_.front());
        // The status pin is active low
        irq_status_port_->provide_inputs(0, kIrqStatusMask);
        interrupt_id_ = irq_->set_interrupt();
      }
    }
    interrupt_clear_ = data;
  });
}

// Translates the SDL keyboard event into a sequence of PS/2 data bytes to be
// put on the data port.
void PS2Keyboard6301::handle_keyboard_event(SDL_KeyboardEvent event) {
  VLOG(1) << "Handling keyboard event on scancode: " << event.keysym.scancode;
  const std::vector<uint8_t>* data = nullptr;
  if (event.type == SDL_KEYDOWN) {
    auto it = sdl_to_ps2_keymap.find(event.keysym.scancode);
    if (it != sdl_to_ps2_keymap.end()) {
      data = &it->second.down;
    }
  } else if (event.type == SDL_KEYUP) {
    auto it = sdl_to_ps2_keymap.find(event.keysym.scancode);
    if (it != sdl_to_ps2_keymap.end()) {
      data = &it->second.up;
    }
  }
  if (data != nullptr && !data->empty()) {
    absl::MutexLock lock(&mutex_);
    for (const auto byte : *data) {
      data_.push(byte);
    }
    // 0 is a sentinel value for no interrupt.
    if (interrupt_id_ == 0) {
      data_port_->provide_inputs(data_.front());
      irq_status_port_->provide_inputs(0, kIrqStatusMask);
      interrupt_id_ = irq_->set_interrupt();
    }
  }
}

}  // namespace eight_bit