#include "graphics.h"

#include <SDL2/SDL.h>
#include <absl/cleanup/cleanup.h>

#include <string>

#include "address_space.h"

void run_test(eight_bit::AddressSpace* address_space, SDL_Keycode key) {
  uint16_t base_address = 0x0000;
  switch (key) {
    case SDLK_0:
      // Clear screen
      address_space->set(base_address + 1, 0x00);
      break;
    case SDLK_1: {
      // Write out "Hello World!"
      std::string hello_world = "Hello World!";
      for (int i = 0; i < hello_world.size(); ++i) {
        address_space->set(base_address, hello_world[i]);
      }
      break;
    }
    case SDLK_2:
      // Clear current line
      address_space->set(base_address + 1, 0x01);
      break;
    case SDLK_3:
      // Clear next line
      address_space->set(base_address + 1, 0x02);
      break;
    case SDLK_4:
      // Move cursor 5 characters back
      address_space->set(base_address + 2, -5);
      break;
    case SDLK_5:
      // Move cursor 5 characters forward
      address_space->set(base_address + 2, 5);
      break;
    case SDLK_6:
      // Move cursor to row 3
      address_space->set(base_address + 5, 3);
      break;
    case SDLK_7:
      // Move cursor to colum 12
      address_space->set(base_address + 4, 12);
      break;
    case SDLK_8:
      // Hide cursor
      static bool cursor_hidden = false;
      cursor_hidden = !cursor_hidden;
      address_space->set(base_address + 8, cursor_hidden);
      break;
    case SDLK_9: {
      // Write 450 "0...9" strings (fill the screen and then some)
      std::string hello_world = "0123456789";
      for (int i = 0; i < 450; ++i) {
        for (int j = 0; j < hello_world.size(); ++j) {
          address_space->set(base_address, hello_world[j]);
        }
      }
      break;
    }
    default:
      break;
  }
}

int main() {
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

  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  eight_bit::AddressSpace address_space;
  eight_bit::Graphics graphics;
  graphics.initialize(0x0000, &address_space);

  SDL_Event event;
  bool running = true;
  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      }
      if (event.type == SDL_KEYDOWN) {
        run_test(&address_space, event.key.keysym.sym);
      }
    }
    graphics.render();
    SDL_Delay(16);
  }
  return 0;
}