#include <SDL2/SDL.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"
#include "ps2_keyboard.h"
#include "ram.h"
#include "rom.h"
#include "sound_opl3.h"
#include "tl16c2550.h"

#ifdef HAVE_MIDI
#include "midi_to_serial.h"
#endif

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM file to load");
ABSL_FLAG(int, ticks_per_second, 1000000, "Number of CPU ticks per second");

// SDL starts one timer thread that runs all timer callbacks. In theory
// SDL_Quit() should shut it down, but instead it seems to jsut stop running the
// callbacks while the thread itself sticks around. With the thread still active
// TSan can't see that accesses from timer_callback and the destructors from the
// main thread do not interact with each other and freks out at shoutdown time.
// This mutex is cheap and avoids the false positive.
ABSL_CONST_INIT absl::Mutex callback_mutex_(absl::kConstInit);

// This gets called every millisecond, which corresponds to 1000 CPU ticks.
// TODO: figure out how to do this faster
Uint32 timer_callback(Uint32 interval, void* param) {
  absl::MutexLock lock(&callback_mutex_);
  const int num_ticks = absl::GetFlag(FLAGS_ticks_per_second) / 1000;
  auto* cpu = static_cast<eight_bit::Cpu6301*>(param);
  for (int i = 0; i < num_ticks; ++i) {
    cpu->tick();
  }
  return interval;
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    LOG(FATAL) << absl::StreamFormat("Failed to initialize SDL: %s",
                                     SDL_GetError());
  }

  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    LOG(ERROR) << "SDL can not disable compositor bypass - not running under "
                  "Linux?";
  }

  eight_bit::AddressSpace address_space;

  auto rom_or = eight_bit::Rom::create(&address_space, 0x8000, 0x8000);
  QCHECK_OK(rom_or);
  auto rom = std::move(rom_or.value());

  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "No ROM file specified.";
  const std::string rom_file_name = absl::GetFlag(FLAGS_rom_file);
  std::ifstream rom_file(rom_file_name, std::ios::binary);
  QCHECK(rom_file.is_open()) << "Failed to open file: " << rom_file_name;

  std::vector<uint8_t> rom_data(std::istreambuf_iterator<char>(rom_file), {});
  rom->load(0, rom_data);

  // 0..1f is internal CPU registers and otherwise reserved
  auto ram_or = eight_bit::Ram::create(&address_space, 0x0020, 0x7f00 - 0x0020);
  QCHECK_OK(ram_or);

  auto graphics_or = eight_bit::Graphics::create(0x7fc0, &address_space);
  QCHECK_OK(graphics_or);
  auto graphics = std::move(graphics_or.value());

  auto cpu_or = eight_bit::Cpu6301::create(&address_space);
  QCHECK_OK(cpu_or);
  auto cpu = std::move(cpu_or.value());
  cpu->reset();
  std::cout << "CPU serial port: " << cpu->get_serial()->get_pty_name() << "\n";

  eight_bit::PS2Keyboard keyboard(cpu->get_irq(), cpu->get_port1(),
                                  cpu->get_port2());

  auto sound_opl3 = eight_bit::SoundOPL3::create(&address_space, 0x7f80);
  QCHECK_OK(sound_opl3);

  auto tl16c2550 =
      eight_bit::TL16C2550::create(&address_space, 0x7f40, cpu->get_irq());
  QCHECK_OK(tl16c2550);

#ifdef HAVE_MIDI
  auto midi_to_serial =
      eight_bit::MidiToSerial::create((*tl16c2550)->get_pty_name(0));
  QCHECK_OK(midi_to_serial);
#endif

  // Add a timer callback to call cpu.tick() once every millisecond
  SDL_TimerID timer = SDL_AddTimer(1, timer_callback, cpu.get());
  if (timer == 0) {
    LOG(FATAL) << absl::StreamFormat("Failed to create timer: %s",
                                     SDL_GetError());
  }

  // Create an event handler
  SDL_Event event;

  // Main loop
  bool running = true;
  while (running) {
    // Handle events
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      }
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        keyboard.handle_keyboard_event(event.key);
      }
    }
    graphics->render();
    // Roughly 30 fps. TODO: make this a timer callback.
    SDL_Delay(16);
  }
  SDL_RemoveTimer(timer);

  // This makes sure that the timer callback has finished before we exit.
  absl::MutexLock lock(&callback_mutex_);

  return 0;
}
