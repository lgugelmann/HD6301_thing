#include "ps2_keyboard.h"

#include <map>
#include <span>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace eight_bit {

namespace {

struct PS2Key {
  std::vector<uint8_t> down;
  std::vector<uint8_t> up;
};

const std::map<SDL_Scancode, PS2Key> sdl_to_ps2_keymap{
    {SDL_SCANCODE_A, {{0x1c}, {0xf0, 0x1c}}},
    {SDL_SCANCODE_B, {{0x32}, {0xf0, 0x32}}},
    {SDL_SCANCODE_C, {{0x21}, {0xf0, 0x21}}},
    {SDL_SCANCODE_D, {{0x23}, {0xf0, 0x23}}},
    {SDL_SCANCODE_E, {{0x24}, {0xf0, 0x24}}},
    {SDL_SCANCODE_F, {{0x2b}, {0xf0, 0x2b}}},
    {SDL_SCANCODE_G, {{0x34}, {0xf0, 0x34}}},
    {SDL_SCANCODE_H, {{0x33}, {0xf0, 0x33}}},
    {SDL_SCANCODE_I, {{0x43}, {0xf0, 0x43}}},
    {SDL_SCANCODE_J, {{0x3b}, {0xf0, 0x3b}}},
    {SDL_SCANCODE_K, {{0x42}, {0xf0, 0x42}}},
    {SDL_SCANCODE_L, {{0x4b}, {0xf0, 0x4b}}},
    {SDL_SCANCODE_M, {{0x3a}, {0xf0, 0x3a}}},
    {SDL_SCANCODE_N, {{0x31}, {0xf0, 0x31}}},
    {SDL_SCANCODE_O, {{0x44}, {0xf0, 0x44}}},
    {SDL_SCANCODE_P, {{0x4d}, {0xf0, 0x4d}}},
    {SDL_SCANCODE_Q, {{0x15}, {0xf0, 0x15}}},
    {SDL_SCANCODE_R, {{0x2d}, {0xf0, 0x2d}}},
    {SDL_SCANCODE_S, {{0x1b}, {0xf0, 0x1b}}},
    {SDL_SCANCODE_T, {{0x2c}, {0xf0, 0x2c}}},
    {SDL_SCANCODE_U, {{0x3c}, {0xf0, 0x3c}}},
    {SDL_SCANCODE_V, {{0x2a}, {0xf0, 0x2a}}},
    {SDL_SCANCODE_W, {{0x1d}, {0xf0, 0x1d}}},
    {SDL_SCANCODE_X, {{0x22}, {0xf0, 0x22}}},
    {SDL_SCANCODE_Y, {{0x35}, {0xf0, 0x35}}},
    {SDL_SCANCODE_Z, {{0x1a}, {0xf0, 0x1a}}},
    {SDL_SCANCODE_1, {{0x16}, {0xf0, 0x16}}},
    {SDL_SCANCODE_2, {{0x1e}, {0xf0, 0x1e}}},
    {SDL_SCANCODE_3, {{0x26}, {0xf0, 0x26}}},
    {SDL_SCANCODE_4, {{0x25}, {0xf0, 0x25}}},
    {SDL_SCANCODE_5, {{0x2e}, {0xf0, 0x2e}}},
    {SDL_SCANCODE_6, {{0x36}, {0xf0, 0x36}}},
    {SDL_SCANCODE_7, {{0x3d}, {0xf0, 0x3d}}},
    {SDL_SCANCODE_8, {{0x3e}, {0xf0, 0x3e}}},
    {SDL_SCANCODE_9, {{0x46}, {0xf0, 0x46}}},
    {SDL_SCANCODE_0, {{0x45}, {0xf0, 0x45}}},
    {SDL_SCANCODE_RETURN, {{0x5a}, {0xf0, 0x5a}}},
    {SDL_SCANCODE_ESCAPE, {{0x76}, {0xf0, 0x76}}},
    {SDL_SCANCODE_BACKSPACE, {{0x66}, {0xf0, 0x66}}},
    {SDL_SCANCODE_TAB, {{0x0d}, {0xf0, 0x0d}}},
    {SDL_SCANCODE_SPACE, {{0x29}, {0xf0, 0x29}}},
    {SDL_SCANCODE_MINUS, {{0x4e}, {0xf0, 0x4e}}},
    {SDL_SCANCODE_EQUALS, {{0x55}, {0xf0, 0x55}}},
    {SDL_SCANCODE_LEFTBRACKET, {{0x54}, {0xf0, 0x54}}},
    {SDL_SCANCODE_RIGHTBRACKET, {{0x5b}, {0xf0, 0x5b}}},
    {SDL_SCANCODE_BACKSLASH, {{0x5d}, {0xf0, 0x5d}}},
    {SDL_SCANCODE_SEMICOLON, {{0x4c}, {0xf0, 0x4c}}},
    {SDL_SCANCODE_APOSTROPHE, {{0x52}, {0xf0, 0x52}}},
    {SDL_SCANCODE_GRAVE, {{0x0e}, {0xf0, 0x0e}}},
    {SDL_SCANCODE_COMMA, {{0x41}, {0xf0, 0x41}}},
    {SDL_SCANCODE_PERIOD, {{0x49}, {0xf0, 0x49}}},
    {SDL_SCANCODE_SLASH, {{0x4a}, {0xf0, 0x4a}}},
    {SDL_SCANCODE_CAPSLOCK, {{0x58}, {0xf0, 0x58}}},
    {SDL_SCANCODE_F1, {{0x05}, {0xf0, 0x05}}},
    {SDL_SCANCODE_F2, {{0x06}, {0xf0, 0x06}}},
    {SDL_SCANCODE_F3, {{0x04}, {0xf0, 0x04}}},
    {SDL_SCANCODE_F4, {{0x0c}, {0xf0, 0x0c}}},
    {SDL_SCANCODE_F5, {{0x03}, {0xf0, 0x03}}},
    {SDL_SCANCODE_F6, {{0x0b}, {0xf0, 0x0b}}},
    {SDL_SCANCODE_F7, {{0x83}, {0xf0, 0x83}}},
    {SDL_SCANCODE_F8, {{0x0a}, {0xf0, 0x0a}}},
    {SDL_SCANCODE_F9, {{0x01}, {0xf0, 0x01}}},
    {SDL_SCANCODE_F10, {{0x09}, {0xf0, 0x09}}},
    {SDL_SCANCODE_F11, {{0x78}, {0xf0, 0x78}}},
    {SDL_SCANCODE_F12, {{0x07}, {0xf0, 0x07}}},
    {SDL_SCANCODE_PRINTSCREEN,
     {{0xe0, 0x12, 0xe0, 0x7c}, {0xe0, 0xf0, 0x12, 0xe0, 0xf0, 0x7c}}},
    {SDL_SCANCODE_SCROLLLOCK, {{0x7e}, {0xf0, 0x7e}}},
    {SDL_SCANCODE_PAUSE,
     {{0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77}, {}}},
    {SDL_SCANCODE_INSERT, {{0xe0, 0x70}, {0xe0, 0xf0, 0x70}}},
    {SDL_SCANCODE_HOME, {{0xe0, 0x6c}, {0xe0, 0xf0, 0x6c}}},
    {SDL_SCANCODE_PAGEUP, {{0xe0, 0x7d}, {0xe0, 0xf0, 0x7d}}},
    {SDL_SCANCODE_DELETE, {{0xe0, 0x71}, {0xe0, 0xf0, 0x71}}},
    {SDL_SCANCODE_END, {{0xe0, 0x69}, {0xe0, 0xf0, 0x69}}},
    {SDL_SCANCODE_PAGEDOWN, {{0xe0, 0x7a}, {0xe0, 0xf0, 0x7a}}},
    {SDL_SCANCODE_RIGHT, {{0xe0, 0x74}, {0xe0, 0xf0, 0x74}}},
    {SDL_SCANCODE_LEFT, {{0xe0, 0x6b}, {0xe0, 0xf0, 0x6b}}},
    {SDL_SCANCODE_DOWN, {{0xe0, 0x72}, {0xe0, 0xf0, 0x72}}},
    {SDL_SCANCODE_UP, {{0xe0, 0x75}, {0xe0, 0xf0, 0x75}}},
    {SDL_SCANCODE_NUMLOCKCLEAR, {{0x77}, {0xf0, 0x77}}},
    {SDL_SCANCODE_KP_DIVIDE, {{0xe0, 0x4a}, {0xe0, 0xf0, 0x4a}}},
    {SDL_SCANCODE_KP_MULTIPLY, {{0x7c}, {0xf0, 0x7c}}},
    {SDL_SCANCODE_KP_MINUS, {{0x7b}, {0xf0, 0x7b}}},
    {SDL_SCANCODE_KP_PLUS, {{0x79}, {0xf0, 0x79}}},
    {SDL_SCANCODE_KP_ENTER, {{0x5a}, {0xf0, 0x5a}}},
    {SDL_SCANCODE_KP_1, {{0x69}, {0xf0, 0x69}}},
    {SDL_SCANCODE_KP_2, {{0x72}, {0xf0, 0x72}}},
    {SDL_SCANCODE_KP_3, {{0x7a}, {0xf0, 0x7a}}},
    {SDL_SCANCODE_KP_4, {{0x6b}, {0xf0, 0x6b}}},
    {SDL_SCANCODE_KP_5, {{0x73}, {0xf0, 0x73}}},
    {SDL_SCANCODE_KP_6, {{0x74}, {0xf0, 0x74}}},
    {SDL_SCANCODE_KP_7, {{0x6c}, {0xf0, 0x6c}}},
    {SDL_SCANCODE_KP_8, {{0x75}, {0xf0, 0x75}}},
    {SDL_SCANCODE_KP_9, {{0x7d}, {0xf0, 0x7d}}},
    {SDL_SCANCODE_KP_0, {{0x70}, {0xf0, 0x70}}},
    {SDL_SCANCODE_KP_PERIOD, {{0x71}, {0xf0, 0x71}}},
    {SDL_SCANCODE_LCTRL, {{0x14}, {0xf0, 0x14}}},
    {SDL_SCANCODE_LSHIFT, {{0x12}, {0xf0, 0x12}}},
    {SDL_SCANCODE_LALT, {{0x11}, {0xf0, 0x11}}},
    {SDL_SCANCODE_RCTRL, {{0xe, 0x14}, {0xe0, 0xf0, 0x14}}},
    {SDL_SCANCODE_RSHIFT, {{0x59}, {0xf0, 0x59}}},
    {SDL_SCANCODE_RALT, {{0xe0, 0x11}, {0xe0, 0xf0, 0x11}}}};

}  // namespace

PS2Keyboard::PS2Keyboard(Interrupt* irq, IOPort* data_port,
                         IOPort* irq_status_port)
    : irq_(irq), data_port_(data_port), irq_status_port_(irq_status_port) {
  // Bit 0 on the irq status port can be written to and is used to clear the
  // keyboard interrupt by being pulled low then high again.
  irq_status_port_->register_output_change_callback([this](uint8_t data) {
    absl::MutexLock lock(&mutex_);
    data = data & 0x01;
    // We saw a 0->1 transition on the interrupt clear bit. Clear the interrupt.
    if (data == 1 && interrupt_clear_ == 0) {
      VLOG(1) << "Clearing keyboard interrupt";
      if (interrupt_id_ != 0) {
        irq_->clear_interrupt(interrupt_id_);
        interrupt_id_ = 0;
        irq_status_port_->provide_inputs(0x02, 0x02);
      }
      // TODO: This should really happen after some given amount of time
      // corresponding to ps2 data rates, but for now just assume that the
      // interrupt is cleared after reading keyboard data.
      if (!data_.empty()) {
        data_.pop();
      }
      if (!data_.empty()) {
        data_port_->provide_inputs(data_.front());
        irq_status_port_->provide_inputs(0, 0x02);
        interrupt_id_ = irq_->set_interrupt();
      }
    }
    interrupt_clear_ = data;
  });
}

// Translates the SDL keyboard event into a sequence of PS/2 data bytes to be
// put on the data port.
void PS2Keyboard::handle_keyboard_event(SDL_KeyboardEvent event) {
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
      irq_status_port_->provide_inputs(0, 0x02);
      interrupt_id_ = irq_->set_interrupt();
    }
  }
}

}  // namespace eight_bit