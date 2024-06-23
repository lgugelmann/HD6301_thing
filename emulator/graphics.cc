#include "graphics.h"

#include <SDL.h>

#include "../pico_graphics/font.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {

Graphics::~Graphics() {
  if (palette_) {
    SDL_FreePalette(palette_);
  }
  if (frame_surface_) {
    SDL_FreeSurface(frame_surface_);
  }
}

absl::StatusOr<std::unique_ptr<Graphics>> Graphics::create(
    uint16_t base_address, AddressSpace* address_space) {
  auto graphics =
      std::unique_ptr<Graphics>(new Graphics(address_space, base_address));
  auto status = graphics->initialize();
  if (!status.ok()) {
    return status;
  }
  return graphics;
}

absl::Status Graphics::initialize() {
  // Initialize a palette with the 64 RGB222 colors
  palette_ = SDL_AllocPalette(64);
  if (!palette_) {
    return absl::InternalError(
        absl::StrCat("Failed to allocate palette: ", SDL_GetError()));
  }
  for (int i = 0; i < 64; ++i) {
    palette_->colors[i].r = ((i & 0b00110000) >> 4) * 85;
    palette_->colors[i].g = ((i & 0b00001100) >> 2) * 85;
    palette_->colors[i].b = (i & 0b00000011) * 85;
  }

  // Create a surface for the frame
  frame_surface_ = SDL_CreateRGBSurfaceWithFormat(0, kFrameWidth, kFrameHeight,
                                                  8, SDL_PIXELFORMAT_INDEX8);
  if (!frame_surface_) {
    return absl::InternalError(
        absl::StrCat("Failed to create frame surface: ", SDL_GetError()));
  }
  SDL_SetSurfacePalette(frame_surface_, palette_);
  // nullptr means fill the entire surface
  SDL_FillRect(frame_surface_, nullptr, 0 /* black */);

  auto status = address_space_->register_write(
      base_address_, base_address_ + 63,
      [this](uint16_t address, uint8_t data) { write(address, data); });
  if (!status.ok()) {
    return status;
  }

  return absl::OkStatus();
}

absl::Status Graphics::render(SDL_Renderer* renderer,
                              SDL_Rect* destination_rect) {
  {
    absl::MutexLock lock(&graphics_state_mutex_);
    if (graphics_state_dirty_) {
      render_console();
    }
  }
  SDL_LockSurface(frame_surface_);
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, frame_surface_);
  SDL_UnlockSurface(frame_surface_);
  if (!texture) {
    return absl::InternalError(
        absl::StrCat("Failed to create texture: ", SDL_GetError()));
  }
  SDL_RenderCopy(renderer, texture, nullptr, destination_rect);
  SDL_DestroyTexture(texture);
  return absl::OkStatus();
}

Graphics::Graphics(AddressSpace* address_space, uint16_t base_address)
    : address_space_(address_space), base_address_(base_address) {}

void Graphics::write(uint16_t address, uint8_t data) {
  const uint8_t command = address - base_address_;
  absl::MutexLock lock(&graphics_state_mutex_);
  graphics_state_.HandleCommand(command, data);
  graphics_state_dirty_ = true;
}

inline void Graphics::draw_character(int position) {
  const int row = position / kNumColumns;
  const int col = position % kNumColumns;
  const int x = col * kFontCharWidth;
  const int y = row * kFontCharHeight;

  const char* characters = graphics_state_.GetCharBuf();
  uint8_t foreground_color = graphics_state_.GetForegroundColor(position);
  uint8_t background_color = graphics_state_.GetBackgroundColor(position);

  SDL_Rect rect = {x, y, kFontCharWidth, kFontCharHeight};
  SDL_FillRect(frame_surface_, &rect, background_color);

  const uint8_t character = characters[position];
  // This simplifies the code below as we can just read a single byte.
  static_assert(kFontCharWidth == 8);
  // The font is stored as a 1bpp bitmap of size kFontNumChars*kFontCharHeight,
  // with each byte representing a row of 8 pixels.
  const uint8_t* font_data = font + character;
  auto* pixels = static_cast<uint8_t*>(frame_surface_->pixels);
  for (int i = 0; i < kFontCharHeight; ++i) {
    for (int j = 0; j < kFontCharWidth; ++j) {
      if ((font_data[i * kFontNumChars] & (1 << j))) {
        pixels[(y + i) * kFrameWidth + x + j] = foreground_color;
      }
    }
  }
}

void Graphics::render_console() {
  SDL_LockSurface(frame_surface_);
  for (int i = 0; i < kNumRows; ++i) {
    for (int j = 0; j < kNumColumns; ++j) {
      const int position = i * kNumColumns + j;
      draw_character(position);
    }
  }
  SDL_UnlockSurface(frame_surface_);
}

}  // namespace eight_bit
