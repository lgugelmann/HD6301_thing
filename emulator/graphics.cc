#include "graphics.h"

#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <absl/log/log.h>

namespace eight_bit {

Graphics::Graphics() {}

Graphics::~Graphics() {
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }
}

int Graphics::initialize(uint16_t base_address, AddressSpace* address_space) {
  base_address_ = base_address;
  window_ = SDL_CreateWindow("Emulator", SDL_WINDOWPOS_UNDEFINED,
                             SDL_WINDOWPOS_UNDEFINED, frame_width_,
                             frame_height_, SDL_WINDOW_SHOWN);
  if (!window_) {
    LOG(ERROR) << "Failed to create window: " << SDL_GetError();
    return -1;
  }

  // Create a renderer
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer_) {
    LOG(ERROR) << "Failed to create renderer: " << SDL_GetError();
    return -1;
  }

  // Set a black background
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);

  if (!address_space->register_write(
          base_address, base_address + 63,
          [this](uint16_t address, uint8_t data) { write(address, data); })) {
    return -1;
  };

  return 0;
}

void Graphics::render() {
  // Clear the renderer
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);

  // Set the font size
  int font_size = 15;
  TTF_Font* font = TTF_OpenFont("../font/BigBlue_TerminalPlus.TTF", font_size);
  if (!font) {
    LOG(ERROR) << "Failed to load font: " << TTF_GetError();
    return;
  }

  // Set the font color
  SDL_Color font_color = {255, 255, 255, 255};

  // Calculate the character size and position
  int console_width = num_columns_;
  int console_height = num_rows_;
  int char_width = font_char_width_;
  int char_height = font_char_height_;
  int start_x = (frame_width_ - console_width * char_width) / 2;
  int start_y = (frame_height_ - console_height * char_height) / 2;

  // Render each character in the text buffer
  for (int row = 0; row < console_height; row++) {
    for (int col = 0; col < console_width; col++) {
      char character = state_.characters[row * console_width + col];
      if (character != '\0') {
        // Create a surface from the character
        SDL_Surface* surface =
            TTF_RenderGlyph_Solid(font, character, font_color);
        if (!surface) {
          LOG(ERROR) << "Failed to render character: " << TTF_GetError();
          continue;
        }

        // Create a texture from the surface
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (!texture) {
          LOG(ERROR) << "Failed to create texture: " << SDL_GetError();
          SDL_FreeSurface(surface);
          continue;
        }

        // Set the position and size of the character
        SDL_Rect dest_rect = {start_x + col * char_width,
                              start_y + row * char_height, char_width,
                              char_height};

        // Render the character texture
        SDL_RenderCopy(renderer_, texture, nullptr, &dest_rect);

        // Clean up
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
      }
    }
  }

  // Destroy the font
  TTF_CloseFont(font);

  // Present the renderer
  SDL_RenderPresent(renderer_);
}

void Graphics::write(uint16_t address, uint8_t data) {
  const uint8_t command = address - base_address_;
  switch (command) {
    // write character, advance cursor
    case 0:
      state_.characters[state_.cursor_pos++] = data;
      if (state_.cursor_pos >= char_buf_size_) {
        state_.cursor_pos = 0;
      }
      break;
    // clear commands:
    // 0: clear all characters, cursor to 0
    // 1: clear current row, cursor to start of row
    // 2: clear next row, cursor to start of next row
    case 1: {
      switch (data) {
        case 0:
          std::fill(state_.characters.begin(), state_.characters.end(), ' ');
          state_.cursor_pos = 0;
          break;
        case 1:
          state_.cursor_pos -= state_.cursor_pos % num_columns_;
          std::fill(
              state_.characters.begin() + state_.cursor_pos,
              state_.characters.begin() + state_.cursor_pos + num_columns_,
              ' ');
          break;
        case 2:
          state_.cursor_pos += num_columns_ - state_.cursor_pos % num_columns_;
          std::fill(
              state_.characters.begin() + state_.cursor_pos,
              state_.characters.begin() + state_.cursor_pos + num_columns_,
              ' ');
          break;
      }
      break;
    }
    // Cursor position delta commands. Data contains signed delta to cursor
    // position.
    case 2:
      state_.cursor_pos += data;
      while (state_.cursor_pos >= char_buf_size_) {
        state_.cursor_pos -= char_buf_size_;
      }
      break;
    // Same as 0 but doesn't advance cursor
    case 3:
      state_.characters[state_.cursor_pos] = data;
      break;
    // Set cursor column
    case 4:
      state_.cursor_pos = state_.cursor_pos -
                          (state_.cursor_pos % num_columns_) +
                          data % num_columns_;
      break;
    // Set cursor row
    case 5:
      state_.cursor_pos = (data % num_rows_) * num_columns_ +
                          (state_.cursor_pos % num_columns_);
      break;
    // set cursor position high byte
    case 6:
      state_.cursor_pos_high = data;
      break;
    // set cursor position low byte and update position as (high << 8) | low
    case 7:
      state_.cursor_pos = (state_.cursor_pos_high << 8) | data;
      break;
    // Set cursor visibility: 0 = visible, 1 = hidden
    case 8:
      state_.cursor_hidden = data;
      break;
    default:
      LOG(ERROR) << "Unknown graphics command: " << command;
  }
}

}  // namespace eight_bit