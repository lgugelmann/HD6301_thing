#include <SDL2/SDL.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "address_space.h"
#include "cpu6301.h"

using eight_bit::AddressSpace;
using eight_bit::Cpu6301;

// This gets called every millisecond, which corresponds to 1000 CPU ticks.
// TODO: figure out how to do this faster
Uint32 timer_callback(Uint32 interval, void* param) {
  Cpu6301* cpu = static_cast<Cpu6301*>(param);
  for (int i = 0; i < 1000; ++i) {
    cpu->tick();
  }
  return interval;
}

int main(int argc, char* argv[]) {
  AddressSpace address_space;

  std::ifstream monitor_file("../../asm/monitor.bin", std::ios::binary);
  if (!monitor_file.is_open()) {
    fprintf(stderr, "Failed to open monitor file.\n");
    return -1;
  }
  std::vector<uint8_t> monitor(std::istreambuf_iterator<char>(monitor_file),
                               {});
  address_space.load(0x10000 - monitor.size(), monitor);

  Cpu6301 cpu(&address_space);
  cpu.reset();

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
    return -1;
  }

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    fprintf(stderr,
            "SDL can not disable compositor bypass - not running under Linux?");
  }

  // Create a window
  SDL_Window* window =
      SDL_CreateWindow("Emulator", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_SHOWN);
  if (!window) {
    fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  // Create a renderer
  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    fprintf(stderr, "Failed to create renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  // Set a black background
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  // Add a timer callback to call cpu.tick() once every millisecond
  SDL_TimerID timerID = SDL_AddTimer(1, timer_callback, &cpu);
  if (timerID == 0) {
    fprintf(stderr, "Failed to create timer: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
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

    // Render the changes to the window
    SDL_RenderPresent(renderer);
    SDL_Delay(1);  // Delay for 1 millisecond
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
