#ifndef EIGHT_BIT_GRAPHICS_H_
#define EIGHT_BIT_GRAPHICS_H_

#include <SDL2/SDL.h>

#include <vector>

#include "address_space.h"

namespace eight_bit {

class Graphics {
 public:
  Graphics();
  ~Graphics();

  int initialize(uint16_t base_address, AddressSpace* address_space);

  void render();

 private:
  void render_character(int position, bool reverse_colors);

  void write(uint16_t address, uint8_t data);

  static constexpr int kFrameWidth = 800;
  static constexpr int kFrameHeight = 600;

  static constexpr int kFontCharWidth = 8;
  static constexpr int kFontCharHeight = 15;
  static constexpr int kFontNumChars = 128;

  static constexpr int kNumColumns = kFrameWidth / kFontCharWidth;
  static constexpr int kNumRows = kFrameHeight / kFontCharHeight;
  static constexpr int kCharBufSize = kNumRows * kNumColumns;
  static constexpr int kFrameSize = kFrameWidth * kFrameHeight;

  int cursor_pos_ = 0;
  int cursor_pos_high_ = 0;
  bool cursor_hidden_ = false;

  std::array<uint8_t, kCharBufSize> characters_ = {0};
  // 0x3f is white, 0 is black
  std::array<uint8_t, kCharBufSize> foreground_color_ = {0x3f};
  std::array<uint8_t, kCharBufSize> background_color_ = {0};
  SDL_Palette* palette_ = nullptr;
  SDL_Surface* frame_surface_ = nullptr;

  uint16_t base_address_ = 0;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_GRAPHICS_H_