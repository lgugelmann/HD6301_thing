#include "hd6301_thing.h"

#include <chrono>
#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"
#include "ps2_keyboard.h"
#include "ram.h"
#include "rom.h"
#include "sound_opl3.h"
#include "spi.h"
#include "tl16c2550.h"
#include "w65c22.h"

#ifdef HAVE_MIDI
#include "midi_to_serial.h"
#endif

namespace eight_bit {

HD6301Thing::~HD6301Thing() {
  emulator_running_ = false;
  if (emulator_thread_.joinable()) {
    emulator_thread_.join();
  }
}

absl::StatusOr<std::unique_ptr<HD6301Thing>> HD6301Thing::create(
    int ticks_per_second) {
  auto hd6301_thing =
      std::unique_ptr<HD6301Thing>(new HD6301Thing(ticks_per_second));
  absl::MutexLock lock(&hd6301_thing->emulator_mutex_);

  constexpr uint rom_start = 0x8000;
  constexpr uint rom_size = 0x8000;
  auto rom_or = eight_bit::Rom::create(&hd6301_thing->address_space_, rom_start,
                                       rom_size);
  if (!rom_or.ok()) {
    return rom_or.status();
  }
  hd6301_thing->rom_ = std::move(rom_or.value());

  // Addresses 0..1f are reserved internal CPU registers.
  auto ram_or = eight_bit::Ram::create(&hd6301_thing->address_space_, 0x0020,
                                       0x7f00 - 0x0020);
  if (!ram_or.ok()) {
    return ram_or.status();
  }
  hd6301_thing->ram_ = std::move(ram_or.value());

  auto graphics_or =
      eight_bit::Graphics::create(0x7fc0, &hd6301_thing->address_space_);
  if (!graphics_or.ok()) {
    return graphics_or.status();
  }
  hd6301_thing->graphics_ = std::move(graphics_or.value());

  auto cpu_or = eight_bit::Cpu6301::create(&hd6301_thing->address_space_);
  if (!cpu_or.ok()) {
    return cpu_or.status();
  }
  hd6301_thing->cpu_ = std::move(cpu_or.value());
  hd6301_thing->cpu_->reset();
  std::cout << "CPU serial port: "
            << hd6301_thing->cpu_->get_serial()->get_pty_name() << std::endl;

  hd6301_thing->keyboard_ = std::make_unique<PS2Keyboard>(
      hd6301_thing->cpu_->get_irq(), hd6301_thing->cpu_->get_port1(),
      hd6301_thing->cpu_->get_port2());

  auto sound_opl3 =
      eight_bit::SoundOPL3::create(&hd6301_thing->address_space_, 0x7f80);
  if (!sound_opl3.ok()) {
    return sound_opl3.status();
  }
  hd6301_thing->sound_opl3_ = std::move(sound_opl3.value());

  auto tl16c2550 = eight_bit::TL16C2550::create(
      &hd6301_thing->address_space_, 0x7f40, hd6301_thing->cpu_->get_irq());
  if (!tl16c2550.ok()) {
    return tl16c2550.status();
  }
  hd6301_thing->tl16c2550_ = std::move(tl16c2550.value());

  auto w65c22_or = eight_bit::W65C22::Create(
      &hd6301_thing->address_space_, 0x7f20, hd6301_thing->cpu_->get_irq());
  if (!w65c22_or.ok()) {
    return w65c22_or.status();
  }
  hd6301_thing->w65c22_ = std::move(w65c22_or.value());
  auto* w65c22_ptr = hd6301_thing->w65c22_.get();
  hd6301_thing->cpu_->register_tick_callback(
      [w65c22_ptr]() { w65c22_ptr->tick(); });

  auto spi =
      eight_bit::SPI::create(hd6301_thing->w65c22_->port_a(), 2, 0, 1, 7);
  if (!spi.ok()) {
    return spi.status();
  }
  hd6301_thing->spi_ = std::move(spi.value());

  // Create an empty 4MB stream for the SD card.
  auto stream = std::make_unique<std::stringstream>(
      std::string(4 * 1024 * 1024, '\0'),
      std::ios::in | std::ios::out | std::ios::binary);
  auto sd_card_spi =
      SDCardSPI::create(hd6301_thing->spi_.get(), std::move(stream));
  if (!sd_card_spi.ok()) {
    return sd_card_spi.status();
  }
  hd6301_thing->sd_card_spi_ = std::move(sd_card_spi.value());

#ifdef HAVE_MIDI
  auto midi_to_serial = eight_bit::MidiToSerial::create(
      hd6301_thing->tl16c2550_->get_pty_name(0));
  if (!midi_to_serial.ok()) {
    return midi_to_serial.status();
  }
  hd6301_thing->midi_to_serial_ = std::move(midi_to_serial.value());
#endif

  hd6301_thing->emulator_running_ = true;
  hd6301_thing->cpu_running_ = false;
  hd6301_thing->emulator_thread_ =
      std::thread(&HD6301Thing::emulator_loop, hd6301_thing.get());

  return hd6301_thing;
}

void HD6301Thing::load_rom(uint16_t address, std::span<uint8_t> data) {
  rom_->load(address, data);
}

void HD6301Thing::load_sd_image(
    std::unique_ptr<std::basic_iostream<char>> image) {
  sd_card_spi_->set_image(std::move(image));
}

void HD6301Thing::handle_keyboard_event(SDL_KeyboardEvent event) {
  absl::MutexLock lock(&emulator_mutex_);
  keyboard_->handle_keyboard_event(event);
}

bool HD6301Thing::is_cpu_running() const { return cpu_running_; }

void HD6301Thing::run() { cpu_running_ = true; }

void HD6301Thing::stop() { cpu_running_ = false; }

void HD6301Thing::tick(int ticks, bool ignore_breakpoint) {
  absl::MutexLock lock(&emulator_mutex_);
  cpu_->tick(ticks, ignore_breakpoint);
}

Cpu6301::CpuState HD6301Thing::get_cpu_state() {
  absl::MutexLock lock(&emulator_mutex_);
  return cpu_->get_state();
}

void HD6301Thing::set_breakpoint(uint16_t address) {
  absl::MutexLock lock(&emulator_mutex_);
  cpu_->set_breakpoint(address);
}

void HD6301Thing::clear_breakpoint() {
  absl::MutexLock lock(&emulator_mutex_);
  cpu_->clear_breakpoint();
}

void HD6301Thing::reset() {
  absl::MutexLock lock(&emulator_mutex_);
  cpu_->reset();
}

std::string HD6301Thing::get_ram_hexdump() {
  absl::MutexLock lock(&emulator_mutex_);
  return ram_->hexdump();
}

absl::Status HD6301Thing::render_graphics(SDL_Renderer* renderer,
                                          SDL_Rect* destination_rect) {
  absl::MutexLock lock(&emulator_mutex_);
  return graphics_->render(renderer, destination_rect);
}

HD6301Thing::HD6301Thing(int ticks_per_second)
    : ticks_per_ms_(ticks_per_second / 1000) {}

void HD6301Thing::emulator_loop() {
  next_loop_time_ = std::chrono::steady_clock::now();
  while (emulator_running_) {
    if (cpu_running_) {
      absl::MutexLock lock(&emulator_mutex_);
      auto result = cpu_->tick(ticks_per_ms_ - extra_ticks_);
      extra_ticks_ = result.cycles_run - ticks_per_ms_;
      if (result.breakpoint_hit) {
        cpu_running_ = false;
      }
    }
    next_loop_time_ += std::chrono::milliseconds(1);
    // Warn if the emulator can't keep up with real time.
    if (next_loop_time_ <
        std::chrono::steady_clock::now() - std::chrono::milliseconds(100)) {
      LOG_EVERY_N_SEC(ERROR, 1)
          << "Emulator is running behind real time by "
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - next_loop_time_)
                 .count()
          << "ms";
    }
    std::this_thread::sleep_until(next_loop_time_);
  }
}

}  // namespace eight_bit