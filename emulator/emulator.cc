#include <SDL2/SDL.h>
#include <absl/cleanup/cleanup.h>
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>
#include <absl/strings/str_format.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"
#include "ps2_keyboard.h"

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM file to load");
ABSL_FLAG(int, ticks_per_second, 1000000, "Number of CPU ticks per second");
ABSL_FLAG(std::optional<uint16_t>, trap_address, std::nullopt,
          "Stop execution on reads or writes to this address, helpful for "
          "setting breakpoints.");

// This gets called every millisecond, which corresponds to 1000 CPU ticks.
// TODO: figure out how to do this faster
Uint32 timer_callback(Uint32 interval, void* param) {
  const int num_ticks = absl::GetFlag(FLAGS_ticks_per_second) / 1000;
  eight_bit::Cpu6301* cpu = static_cast<eight_bit::Cpu6301*>(param);
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
    LOG(ERROR) << absl::StreamFormat("Failed to initialize SDL: %s",
                                     SDL_GetError());
    return -1;
  }

  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    LOG(ERROR) << "SDL can not disable compositor bypass - not running under "
                  "Linux?";
  }

  eight_bit::AddressSpace address_space;

  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "No ROM file specified.";
  const std::string rom_file = absl::GetFlag(FLAGS_rom_file);
  std::ifstream monitor_file(rom_file, std::ios::binary);
  QCHECK(monitor_file.is_open()) << "Failed to open file: " << rom_file;

  std::vector<uint8_t> monitor(std::istreambuf_iterator<char>(monitor_file),
                               {});
  address_space.load(0x10000 - monitor.size(), monitor);

  // This creates some easy-to-breakpoint memory reads for debugging
  uint8_t trap_byte = 0;
  if (absl::GetFlag(FLAGS_trap_address).has_value()) {
    const uint16_t trap_address = absl::GetFlag(FLAGS_trap_address).value();
    trap_byte = address_space.get(trap_address);
    address_space.register_read(
        trap_address, trap_address, [&trap_byte](uint16_t address) -> uint8_t {
          VLOG(1) << absl::StreamFormat("Trap read at %04x: %02x", address,
                                        trap_byte);
          return trap_byte;
        });
    address_space.register_write(trap_address, trap_address,
                                 [&trap_byte](uint16_t address, uint8_t data) {
                                   VLOG(1) << absl::StreamFormat(
                                       "Trap write at %04x: %02x", address,
                                       data);
                                   trap_byte = data;
                                 });
  }

  eight_bit::Graphics graphics;
  if (graphics.initialize(0x7fc0, &address_space) != 0) {
    LOG(ERROR) << "Failed to initialize graphics";
    return -1;
  }

  eight_bit::Cpu6301 cpu(&address_space);
  cpu.reset();

  eight_bit::PS2Keyboard keyboard(cpu.get_irq(), cpu.get_port1(),
                                  cpu.get_port2());

  // Add a timer callback to call cpu.tick() once every millisecond
  SDL_TimerID timer = SDL_AddTimer(1, timer_callback, &cpu);
  if (timer == 0) {
    LOG(ERROR) << absl::StreamFormat("Failed to create timer: %s",
                                     SDL_GetError());
    return -1;
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
    graphics.render();
    // Roughly 30 fps. TODO: make this a timer callback.
    SDL_Delay(16);
  }

  return 0;
}
