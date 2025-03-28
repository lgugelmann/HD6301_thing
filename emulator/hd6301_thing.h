#ifndef EIGHT_BIT_HD6301_THING_H
#define EIGHT_BIT_HD6301_THING_H

#include <chrono>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"
#include "ps2_keyboard_6301.h"
#include "ram.h"
#include "rom.h"
#include "sd_card_spi.h"
#include "sound_opl3.h"
#include "spi.h"
#include "tl16c2550.h"
#include "w65c22.h"
#include "w65c22_to_spi_glue.h"

#ifdef HAVE_MIDI
#include "midi_to_serial.h"
#endif

namespace eight_bit {

class HD6301Thing {
 public:
  HD6301Thing(const HD6301Thing&) = delete;
  HD6301Thing& operator=(const HD6301Thing&) = delete;
  ~HD6301Thing();

  enum KeyboardType {
    kKeyboard6301,
    kKeyboard65C22,
  };

  static absl::StatusOr<std::unique_ptr<HD6301Thing>> create(
      int ticks_per_second, KeyboardType keyboard_type);

  void load_rom(uint16_t address, std::span<uint8_t> data);
  void load_sd_image(std::unique_ptr<std::basic_iostream<char>> image);
  void handle_keyboard_event(SDL_KeyboardEvent event);
  bool is_cpu_running() const;
  void run();
  void stop();
  void tick(int ticks, bool ignore_breakpoint = false);
  Cpu6301::CpuState get_cpu_state();
  void set_breakpoint(uint16_t address);
  void clear_breakpoint();
  void reset();
  std::string get_ram_hexdump();
  absl::Status render_graphics(SDL_Renderer* renderer,
                               SDL_FRect* destination_rect = nullptr);

 private:
  HD6301Thing(int ticks_per_second, KeyboardType keyboard_type);

  void emulator_loop();

  // Which kind of keyboard connection to emulate.
  const KeyboardType keyboard_type_;

  // Protects access to the emulator state for parts that aren't already
  // thread-safe.
  absl::Mutex emulator_mutex_;

  AddressSpace address_space_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<Rom> rom_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<Ram> ram_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<Graphics> graphics_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<Cpu6301> cpu_ ABSL_GUARDED_BY(emulator_mutex_);
  // Thread safe. For the responsiveness of the UI, we don't want to block
  // keycode handling on the emulator running a large number of cycles.
  std::unique_ptr<PS2Keyboard6301> keyboard_6301_;
  std::unique_ptr<SoundOPL3> sound_opl3_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<TL16C2550> tl16c2550_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<W65C22> w65c22_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<W65C22ToSPIGlue> w65c22_to_spi_glue_
      ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<SPI> spi_ ABSL_GUARDED_BY(emulator_mutex_);
  std::unique_ptr<SDCardSPI> sd_card_spi_ ABSL_GUARDED_BY(emulator_mutex_);
#ifdef HAVE_MIDI
  std::unique_ptr<MidiToSerial> midi_to_serial_
      ABSL_GUARDED_BY(emulator_mutex_);
#endif

  // Locking the mutex to read these is expensive enough to show up on profiles.
  // These atomic booelans are significantly faster.
  std::atomic<bool> cpu_running_ = false;
  std::atomic<bool> emulator_running_ = true;

  std::thread emulator_thread_;
  // variables used only in the emulator thread. Don't touch them outside as
  // they are not mutex-protected.
  const int ticks_per_ms_ = 1000;
  int extra_ticks_ = 0;
  std::chrono::time_point<std::chrono::steady_clock> next_loop_time_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_HD6301_THING_H