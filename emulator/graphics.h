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
  void write(uint16_t address, uint8_t data);

  struct GraphicsState {
    GraphicsState(int buffer_size) : characters(buffer_size, 0) {}
    std::vector<uint8_t> characters;
    int cursor_pos = 0;
    int cursor_pos_high = 0;
    bool cursor_hidden = false;
  };

  const int frame_width_ = 800;
  const int frame_height_ = 600;

  const int font_char_width_ = 8;
  const int font_char_height_ = 15;
  const int font_num_chars_ = 128;

  const int num_columns_ = frame_width_ / font_char_width_;
  const int num_rows_ = frame_height_ / font_char_height_;
  const int char_buf_size_ = num_rows_ * num_columns_;

  GraphicsState state_{char_buf_size_};

  uint16_t base_address_ = 0;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_GRAPHICS_H_