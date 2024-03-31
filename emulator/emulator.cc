#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <absl/cleanup/cleanup.h>
#include <absl/log/check.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"

using eight_bit::AddressSpace;
using eight_bit::Cpu6301;
using eight_bit::Graphics;

// This gets called every millisecond, which corresponds to 1000 CPU ticks.
// TODO: figure out how to do this faster
Uint32 timer_callback(Uint32 interval, void* param) {
  const int num_ticks = 1000;
  Cpu6301* cpu = static_cast<Cpu6301*>(param);
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
    fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
    return -1;
  }

  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  if (TTF_Init() < 0) {
    fprintf(stderr, "Failed to initialize TTF: %s\n", TTF_GetError());
    return -1;
  }

  absl::Cleanup ttf_cleanup([] { TTF_Quit(); });

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    fprintf(stderr,
            "SDL can not disable compositor bypass - not running under Linux?");
  }

  AddressSpace address_space;

  std::ifstream monitor_file("../../asm/monitor.bin", std::ios::binary);
  if (!monitor_file.is_open()) {
    fprintf(stderr, "Failed to open monitor file.\n");
    return -1;
  }
  std::vector<uint8_t> monitor(std::istreambuf_iterator<char>(monitor_file),
                               {});
  address_space.load(0x10000 - monitor.size(), monitor);

  eight_bit::Graphics graphics;
  if (graphics.initialize(0x7fc0, &address_space) != 0) {
    fprintf(stderr, "Failed to initialize graphics.\n");
    return -1;
  }

  Cpu6301 cpu(&address_space);
  cpu.reset();

  // Add a timer callback to call cpu.tick() once every millisecond
  SDL_TimerID timerID = SDL_AddTimer(1, timer_callback, &cpu);
  if (timerID == 0) {
    fprintf(stderr, "Failed to create timer: %s\n", SDL_GetError());
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
      // Add keyboard event handling here
    }
    graphics.render();
    // Roughly 30 fps. TODO: make this a timer callback.
    SDL_Delay(16);
  }

  return 0;
}
