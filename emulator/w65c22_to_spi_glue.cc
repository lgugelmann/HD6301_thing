#include "w65c22_to_spi_glue.h"

#include <cstdint>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "sdl_to_ps2_keymap.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<W65C22ToSPIGlue>>
eight_bit::W65C22ToSPIGlue::create(IOPort* clk_in_port, uint8_t clk_in_pin,
                                   IOPort* output_switch_port,
                                   uint8_t output_switch_pin,
                                   IOPort* keyboard_irq_port,
                                   uint8_t keyboard_irq_pin,
                                   IOPort* parallel_out_port) {
  auto miso_to_parallel = std::unique_ptr<W65C22ToSPIGlue>(new W65C22ToSPIGlue(
      clk_in_port, clk_in_pin, output_switch_port, output_switch_pin,
      keyboard_irq_port, keyboard_irq_pin, parallel_out_port));
  return miso_to_parallel;
}

void W65C22ToSPIGlue::tick() {
  if (clock_on_last_tick_) {
    clk_out_port_.write_output_register(prev_clock_ << kClkPin);
    clock_on_last_tick_ = false;
    if (prev_clock_ == 1) {
      // Rising edge, sample MISO
      miso_bit_in(miso_port_.read_input_register());
    }
  }
  if (keyboard_tick_countdown_ == 0) {
    absl::MutexLock lock(&mutex_);
    if (!keyboard_data_queue_.empty()) {
      keyboard_data_ = keyboard_data_queue_.front();
      keyboard_data_queue_.pop();
      keyboard_tick_countdown_ = kKeyboardByteTickInterval + 1;
      // Provide a short edge transition on the keyboard IRQ pin.
      keyboard_irq_port_->provide_inputs(keyboard_irq_mask_,
                                         keyboard_irq_mask_);
      keyboard_irq_port_->provide_inputs(0, keyboard_irq_mask_);
    }
  }
  --keyboard_tick_countdown_;
}

IOPort* W65C22ToSPIGlue::clk_out_port() { return &clk_out_port_; }

IOPort* W65C22ToSPIGlue::miso_port() { return &miso_port_; }

W65C22ToSPIGlue::W65C22ToSPIGlue(IOPort* clk_in_port, uint8_t clk_in_pin,
                                 IOPort* output_switch_port,
                                 uint8_t output_switch_pin,
                                 IOPort* keyboard_irq_port,
                                 uint8_t keyboard_irq_pin,
                                 IOPort* parallel_out_port)
    : clk_out_port_("W65C22 SPI glue clock out"),
      miso_port_("W65C22 SPI glue miso"),
      clk_in_port_(clk_in_port),
      clk_in_mask_(1 << clk_in_pin),
      output_switch_port_(output_switch_port),
      output_switch_mask_(1 << output_switch_pin),
      keyboard_irq_port_(keyboard_irq_port),
      keyboard_irq_mask_(1 << keyboard_irq_pin),
      parallel_out_port_(parallel_out_port) {
  clk_out_port_.write_data_direction_register(kClkBitmask);
  miso_port_.write_data_direction_register(kMisoBitmask);
  miso_port_.register_output_change_callback(
      [this](uint8_t data) { miso_bit_in(data); });
  clk_in_port_->register_output_change_callback(
      [this](uint8_t data) { clk_bit_in(data); });
  output_switch_port_->register_output_change_callback([this](uint8_t data) {
    output_keyboard_data_ = (data & output_switch_mask_) != 0;
    absl::MutexLock lock(&mutex_);
    if (output_keyboard_data_) {
      parallel_out_port_->provide_inputs(keyboard_data_);
    } else {
      parallel_out_port_->provide_inputs(sd_card_data_);
    }
  });
}

void W65C22ToSPIGlue::clk_bit_in(uint8_t data) {
  clock_on_last_tick_ = true;
  prev_clock_ = (data & clk_in_mask_) != 1;
}

void W65C22ToSPIGlue::miso_bit_in(uint8_t data) {
  uint8_t bit = (data & kMisoBitmask) == kMisoBitmask;
  miso_shift_data_ = (miso_shift_data_ << 1) | bit;
  ++shift_count_;
  if (shift_count_ == 8) {
    shift_count_ = 0;
    absl::MutexLock lock(&mutex_);
    sd_card_data_ = miso_shift_data_;
    if (!output_keyboard_data_) {
      parallel_out_port_->provide_inputs(sd_card_data_);
    }
  }
}

void W65C22ToSPIGlue::handle_keyboard_event(SDL_KeyboardEvent event) {
  VLOG(1) << "Handling keyboard event on scancode: " << event.scancode;
  const std::vector<uint8_t>* data = nullptr;
  if (event.type == SDL_EVENT_KEY_DOWN) {
    auto it = sdl_to_ps2_keymap.find(event.scancode);
    if (it != sdl_to_ps2_keymap.end()) {
      data = &it->second.down;
    }
  } else if (event.type == SDL_EVENT_KEY_UP) {
    auto it = sdl_to_ps2_keymap.find(event.scancode);
    if (it != sdl_to_ps2_keymap.end()) {
      data = &it->second.up;
    }
  }
  if (data != nullptr && !data->empty()) {
    absl::MutexLock lock(&mutex_);
    for (const auto byte : *data) {
      keyboard_data_queue_.push(byte);
      keyboard_tick_countdown_ = kKeyboardByteTickInterval;
    }
  }
}

}  // namespace eight_bit