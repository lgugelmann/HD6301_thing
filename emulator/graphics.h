#ifndef EIGHT_BIT_GRAPHICS_H_
#define EIGHT_BIT_GRAPHICS_H_

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "address_space.h"
#include "graphics_state.h"

namespace eight_bit {

class Graphics {
 public:
  ~Graphics();
  Graphics(const Graphics&) = delete;
  Graphics& operator=(const Graphics&) = delete;

  static absl::StatusOr<std::unique_ptr<Graphics>> create(
      uint16_t base_address, AddressSpace* address_space);

  // Render the screen to the given renderer. If destination_rect is not null,
  // the screen will be scaled to fit within the given rectangle. Otherwise it
  // fills the entire renderer.
  absl::Status render(SDL_Renderer* renderer,
                      SDL_FRect* destination_rect = nullptr)
      ABSL_LOCKS_EXCLUDED(graphics_state_mutex_);

 private:
  Graphics(AddressSpace* address_space, uint16_t base_address);
  absl::Status initialize();

  void write(uint16_t address, uint8_t data)
      ABSL_LOCKS_EXCLUDED(graphics_state_mutex_);
  void draw_character(int position)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(graphics_state_mutex_);
  void render_console() ABSL_EXCLUSIVE_LOCKS_REQUIRED(graphics_state_mutex_);

  AddressSpace* address_space_ = nullptr;
  uint16_t base_address_ = 0;

  SDL_Palette* palette_ = nullptr;
  SDL_Surface* frame_surface_ = nullptr;

  absl::Mutex graphics_state_mutex_;
  GraphicsState graphics_state_ ABSL_GUARDED_BY(graphics_state_mutex_);
  bool graphics_state_dirty_ ABSL_GUARDED_BY(graphics_state_mutex_) = false;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_GRAPHICS_H_