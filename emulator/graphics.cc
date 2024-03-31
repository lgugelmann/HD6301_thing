#include "graphics.h"

#include <SDL2/SDL.h>
#include <absl/log/log.h>

#include "../pico_graphics/font.h"

namespace eight_bit {

Graphics::Graphics() {}

Graphics::~Graphics() {
  if (palette_) {
    SDL_FreePalette(palette_);
  }
  if (frame_surface_) {
    SDL_FreeSurface(frame_surface_);
  }
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
                             SDL_WINDOWPOS_UNDEFINED, kFrameWidth, kFrameHeight,
                             SDL_WINDOW_SHOWN);
  if (!window_) {
    LOG(ERROR) << "Failed to create window: " << SDL_GetError();
    return -1;
  }

  // Initialize a palette with the 64 RGB222 colors
  palette_ = SDL_AllocPalette(64);
  if (!palette_) {
    LOG(ERROR) << "Failed to allocate palette: " << SDL_GetError();
    return -1;
  }
  for (int i = 0; i < 64; ++i) {
    palette_->colors[i].r = (i & 0b00000011) * 85;
    palette_->colors[i].g = ((i & 0b00001100) >> 2) * 85;
    palette_->colors[i].b = ((i & 0b00110000) >> 4) * 85;
  }

  // Create a surface for the frame
  frame_surface_ = SDL_CreateRGBSurfaceWithFormat(0, kFrameWidth, kFrameHeight,
                                                  8, SDL_PIXELFORMAT_INDEX8);
  if (!frame_surface_) {
    LOG(ERROR) << "Failed to create frame surface: " << SDL_GetError();
    return -1;
  }
  SDL_SetSurfacePalette(frame_surface_, palette_);
  // nullptr means fill the entire surface
  SDL_FillRect(frame_surface_, nullptr, 0);

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
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);

  SDL_LockSurface(frame_surface_);
  SDL_Texture* texture =
      SDL_CreateTextureFromSurface(renderer_, frame_surface_);
  SDL_UnlockSurface(frame_surface_);
  if (!texture) {
    LOG(ERROR) << "Failed to create texture: " << SDL_GetError();
    return;
  }
  SDL_RenderCopy(renderer_, texture, nullptr, nullptr);
  SDL_DestroyTexture(texture);

  SDL_RenderPresent(renderer_);
}

void Graphics::render_character(int position, bool reverse_colors = false) {
  const int row = position / kNumColumns;
  const int col = position % kNumColumns;
  const int x = col * kFontCharWidth;
  const int y = row * kFontCharHeight;

  SDL_LockSurface(frame_surface_);
  SDL_Rect rect = {x, y, kFontCharWidth, kFontCharHeight};
  uint8_t foreground_color = foreground_color_[position];
  uint8_t background_color = background_color_[position];
  if (reverse_colors) {
    std::swap(foreground_color, background_color);
  }
  SDL_FillRect(frame_surface_, &rect, background_color);

  const uint8_t character = characters_[position];
  // This simplifies the code below as we can just read a single byte.
  static_assert(kFontCharWidth == 8);
  // The font is stored as a 1bpp bitmap of size kFontNumChars*kFontCharHeight,
  // with each byte representing a row of 8 pixels.
  const uint8_t* font_data = font + character;
  uint8_t* pixels = static_cast<uint8_t*>(frame_surface_->pixels);
  for (int i = 0; i < kFontCharHeight; ++i) {
    for (int j = 0; j < kFontCharWidth; ++j) {
      if ((font_data[i * kFontNumChars] & (1 << j))) {
        pixels[(y + i) * kFrameWidth + x + j] = foreground_color;
      }
    }
  }
  SDL_UnlockSurface(frame_surface_);
}

void Graphics::write(uint16_t address, uint8_t data) {
  const uint8_t command = address - base_address_;
  switch (command) {
    // write character, advance cursor
    case 0: {
      int previous_cursor_pos = cursor_pos_;
      characters_[cursor_pos_] = data;
      if (++cursor_pos_ >= kCharBufSize) {
        cursor_pos_ = 0;
      }
      if (!cursor_hidden_) {
        render_character(previous_cursor_pos);
        render_character(cursor_pos_, true);
      }
      break;
    }
    // clear commands
    case 1: {
      switch (data) {
        // 0: clear all characters, cursor to 0, colors to default white on
        // black
        case 0:
          std::fill(characters_.begin(), characters_.end(), ' ');
          std::fill(foreground_color_.begin(), foreground_color_.end(), 0x3f);
          std::fill(background_color_.begin(), background_color_.end(), 0);
          SDL_FillRect(frame_surface_, nullptr, 0);
          cursor_pos_ = 0;
          if (!cursor_hidden_) {
            render_character(cursor_pos_, true);
          }
          break;
        // 1: clear current row, cursor to start of row, colors to default
        case 1: {
          cursor_pos_ -= cursor_pos_ % kNumColumns;
          std::fill(characters_.begin() + cursor_pos_,
                    characters_.begin() + cursor_pos_ + kNumColumns, ' ');
          std::fill(foreground_color_.begin() + cursor_pos_,
                    foreground_color_.begin() + cursor_pos_ + kNumColumns,
                    0x3f);
          std::fill(background_color_.begin() + cursor_pos_,
                    background_color_.begin() + cursor_pos_ + kNumColumns, 0);
          SDL_Rect rect = {0, cursor_pos_ / kNumColumns * kFontCharHeight,
                           kFrameWidth, kFontCharHeight};
          SDL_FillRect(frame_surface_, &rect, 0);
          if (!cursor_hidden_) {
            render_character(cursor_pos_, true);
          }
          break;
        }
        // 2: clear next row, cursor to start of next row, colors to default
        case 2: {
          int previous_cursor_pos = cursor_pos_;
          cursor_pos_ =
              (cursor_pos_ + kNumColumns - cursor_pos_ % kNumColumns) %
              kCharBufSize;
          std::fill(characters_.begin() + cursor_pos_,
                    characters_.begin() + cursor_pos_ + kNumColumns, ' ');
          std::fill(foreground_color_.begin() + cursor_pos_,
                    foreground_color_.begin() + cursor_pos_ + kNumColumns,
                    0x3f);
          std::fill(background_color_.begin() + cursor_pos_,
                    background_color_.begin() + cursor_pos_ + kNumColumns, 0);
          SDL_Rect rect = {0, cursor_pos_ / kNumColumns * kFontCharHeight,
                           kFrameWidth, kFontCharHeight};
          SDL_FillRect(frame_surface_, &rect, 0);
          if (!cursor_hidden_ && previous_cursor_pos != cursor_pos_) {
            render_character(previous_cursor_pos);
            render_character(cursor_pos_, true);
          }
          break;
        }
      }
      break;
    }
    // Cursor position delta commands. Data contains signed delta to cursor
    // position.
    case 2: {
      int previous_cursor_pos = cursor_pos_;
      cursor_pos_ += data;
      while (cursor_pos_ >= kCharBufSize) {
        cursor_pos_ -= kCharBufSize;
      }
      if (!cursor_hidden_ && previous_cursor_pos != cursor_pos_) {
        render_character(previous_cursor_pos);
        render_character(cursor_pos_, true);
      }
      break;
    }
    // Same as 0 but doesn't advance cursor
    case 3:
      characters_[cursor_pos_] = data;
      render_character(cursor_pos_, cursor_hidden_);
      break;
    // Set cursor column
    case 4: {
      int previous_cursor_pos = cursor_pos_;
      cursor_pos_ =
          cursor_pos_ - (cursor_pos_ % kNumColumns) + data % kNumColumns;
      if (!cursor_hidden_ && previous_cursor_pos != cursor_pos_) {
        render_character(previous_cursor_pos);
        render_character(cursor_pos_, true);
      }
      break;
    }
    // Set cursor row
    case 5: {
      int previous_cursor_pos = cursor_pos_;
      cursor_pos_ =
          (data % kNumRows) * kNumColumns + (cursor_pos_ % kNumColumns);
      break;
      if (!cursor_hidden_ && previous_cursor_pos != cursor_pos_) {
        render_character(previous_cursor_pos);
        render_character(cursor_pos_, true);
      }
    }
    // set cursor position high byte
    case 6:
      cursor_pos_high_ = data;
      break;
    // set cursor position low byte and update position as (high << 8) | low
    case 7: {
      int previous_cursor_pos = cursor_pos_;
      cursor_pos_ = (cursor_pos_high_ << 8) | data;
      if (!cursor_hidden_ && previous_cursor_pos != cursor_pos_) {
        render_character(previous_cursor_pos);
        render_character(cursor_pos_, true);
      }
      break;
    }
    // Set cursor visibility: 0 = visible, 1 = hidden
    case 8:
      if (cursor_hidden_ != data) {
        cursor_hidden_ = data;
        render_character(cursor_pos_, true);
      }
      break;
    // Set color at cursor position. Bit format is (MSB first) ABRRGGBB where if
    // A is 1 the cursor advances after setting the color, B is either 0 for
    // foreground or 1 for background. RR/GG/BB are 2-bit Red, Green, Blue
    // channel colors.
    case 9: {
      if (data & 0x40 == 0) {
        foreground_color_[cursor_pos_] = data & 0x3f;
      } else {
        background_color_[cursor_pos_] = data & 0x3f;
      }
      bool advance_cursor = data & 0x80;
      render_character(cursor_pos_, ~advance_cursor & cursor_hidden_);
      if (data & 0x80) {
        if (++cursor_pos_ >= kCharBufSize) {
          cursor_pos_ = 0;
        }
        if (!cursor_hidden_) {
          render_character(cursor_pos_, true);
        }
      }
      break;
    }
    default:
      LOG(ERROR) << "Unknown graphics command: " << command;
  }
}

}  // namespace eight_bit
