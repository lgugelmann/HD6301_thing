#include "graphics_state.h"

#include <cstdio>
#include <cstring>

// If this code is included in the pico_graphics project, we need to make sure
// performance-relevant functions are put in RAM.
#ifdef PICO_COPY_TO_RAM
#include "pico/platform.h"
#define GRAPHICS_NOFLASH __not_in_flash("main")
#else
#define GRAPHICS_NOFLASH
#endif

namespace eight_bit {

GRAPHICS_NOFLASH void GraphicsState::HandleCommand(uint8_t command,
                                                   uint8_t data) {
  switch (command) {
    // Write character, advance cursor
    case 0: {
      charbuf_[cursor_pos_] = data & 0x7f;
      CursorColorFlip();
      cursor_pos_ = (cursor_pos_ + 1) % kCharBufSize;
      CursorColorFlip();
      break;
    }
    // Clear commands
    case 1: {
      switch (data) {
        case 0: {  // Clear full screen, reset cursor to 0, colors to white on
                   // black
          memset(charbuf_, ' ', kCharBufSize);
          memset(colorbuf_, 0x33, 3 * kColorPlaneSizeWords * sizeof(uint32_t));
          cursor_pos_ = 0;
          CursorColorFlip();
          break;
        }
        case 1: {  // Clear current row, reset cursor to row start
          cursor_pos_ -= (cursor_pos_ % kNumColumns);
          memset(charbuf_ + cursor_pos_, ' ', kNumColumns);
          for (int position = cursor_pos_; position < cursor_pos_ + kNumColumns;
               ++position) {
            SetColor(position, 0xff, 0);
          }
          CursorColorFlip();
          break;
        }
        case 2: {  // Clear next row, reset cursor to next row start
          CursorColorFlip();
          cursor_pos_ =
              (cursor_pos_ - (cursor_pos_ % kNumColumns) + kNumColumns) %
              kCharBufSize;
          memset(charbuf_ + cursor_pos_, ' ', kNumColumns);
          for (int position = cursor_pos_; position < cursor_pos_ + kNumColumns;
               ++position) {
            SetColor(position, 0xff, 0);
          }
          CursorColorFlip();
          break;
        }
      }
      break;
    }
    // Cursor position delta commands. Data contains signed delta to cursor
    // position.
    case 2: {
      int new_position = (cursor_pos_ + (signed char)data) % kCharBufSize;
      if (new_position < 0) {
        new_position += kCharBufSize;
      }
      CursorColorFlip();
      cursor_pos_ = new_position;
      CursorColorFlip();
      break;
    }
    // Same as 0 but doesn't advance cursor
    case 3: {
      charbuf_[cursor_pos_] = data & 0x7f;
      break;
    }
    // Set cursor column
    case 4: {
      CursorColorFlip();
      cursor_pos_ =
          cursor_pos_ - (cursor_pos_ % kNumColumns) + data % kNumColumns;
      CursorColorFlip();
      break;
    }
    // Set cursor row
    case 5: {
      CursorColorFlip();
      cursor_pos_ =
          (data % kNumRows) * kNumColumns + (cursor_pos_ % kNumColumns);
      CursorColorFlip();
      break;
    }
    // Set cursor position high byte
    case 6: {
      cursor_pos_high_ = data;
      break;
    }
    // Set cursor position low byte
    case 7: {
      CursorColorFlip();
      cursor_pos_ = (cursor_pos_high_ << 8) + data;
      CursorColorFlip();
      break;
    }
    // Cursor visibility. 0: shown, 1: hidden
    case 8: {
      // If the hidden status changed only one of the flips will be done, if it
      // hasn't we flip 0 or two times. In all cases we get the right result.
      CursorColorFlip();
      cursor_hidden_ = data;
      CursorColorFlip();
      break;
    }
    // Set color at cursor position. Bit format is (MSB first) ABRRGGBB where if
    // A is 1 the cursor advances after setting the color, B is either 0 for
    // foreground or 1 for background. RR/GG/BB are 2-bit Red, Green, Blue
    // channel colors.
    case 9: {
      CursorColorFlip();
      if ((data & 0x40) == 0) {
        // foreground
        SetFgColor(cursor_pos_, data & 0x3f);
      } else {
        // background
        SetBgColor(cursor_pos_, data & 0x3f);
      }
      if (data & 0x80) {
        cursor_pos_ = (cursor_pos_ + 1) % kCharBufSize;
      }
      CursorColorFlip();
      break;
    }
    default:
      break;
  }
}

uint8_t GraphicsState::GetBackgroundColor(int position) const {
  if (position >= kCharBufSize) {
    return 0;
  }
  unsigned int line_padding = (position / kNumColumns) * (kNumColumns % 8);
  unsigned int word_index = (position + line_padding) / 8;
  unsigned int bit_index = (position + line_padding) % 8 * 4 + 2;
  // The color is 00RRGGBB with RR/GG/BB being the 2 color bits in each plane.
  // The planes are in G / B / R order.
  uint8_t color = 0;
  for (int plane = 0; plane < 3; ++plane) {
    color |= ((colorbuf_[word_index + plane * kColorPlaneSizeWords] &
               (0x3u << bit_index)) >>
              bit_index)
             << 2 * plane;
  }
  return color;
}

uint8_t GraphicsState::GetForegroundColor(int position) const {
  if (position >= kCharBufSize) {
    return 0;
  }
  unsigned int line_padding = (position / kNumColumns) * (kNumColumns % 8);
  unsigned int word_index = (position + line_padding) / 8;
  unsigned int bit_index = (position + line_padding) % 8 * 4;
  uint8_t color = 0;
  for (int plane = 0; plane < 3; ++plane) {
    color |= ((colorbuf_[word_index + plane * kColorPlaneSizeWords] &
               (0x3u << bit_index)) >>
              bit_index)
             << 2 * plane;
  }
  return color;
}

GRAPHICS_NOFLASH void GraphicsState::SetColorBits(unsigned int position,
                                                  uint8_t color,
                                                  uint8_t bit_offset) {
  if (position >= kCharBufSize) {
    return;
  }
  // We need to account for some extra padding in the color plane vs the
  // character buffer. With each line we may get an extra word.
  unsigned int line_padding = (position / kNumColumns) * (kNumColumns % 8);
  unsigned int bit_index = (position + line_padding) % 8 * 4 + bit_offset;
  unsigned int word_index = (position + line_padding) / 8;
  for (int plane = 0; plane < 3; ++plane) {
    uint32_t color_masked = color & 0x3;
    colorbuf_[word_index] = (colorbuf_[word_index] & ~(0x3u << bit_index)) |
                            (color_masked << bit_index);
    color >>= 2;
    word_index += kColorPlaneSizeWords;
  }
}

GRAPHICS_NOFLASH inline void GraphicsState::SetFgColor(unsigned int position,
                                                       uint8_t fg) {
  SetColorBits(position, fg, 0);
}

GRAPHICS_NOFLASH inline void GraphicsState::SetBgColor(unsigned int position,
                                                       uint8_t bg) {
  SetColorBits(position, bg, 2);
}

GRAPHICS_NOFLASH inline void GraphicsState::SetColor(unsigned int position,
                                                     uint8_t fg, uint8_t bg) {
  SetFgColor(position, fg);
  SetBgColor(position, bg);
}

// Flips fg/bg color at the cursor if needed
GRAPHICS_NOFLASH void GraphicsState::CursorColorFlip() {
  if (cursor_hidden_) {
    return;
  }

  unsigned int line_padding = (cursor_pos_ / kNumColumns) * (kNumColumns % 8);
  unsigned int word_index = (cursor_pos_ + line_padding) / 8;
  unsigned int bit_index = (cursor_pos_ + line_padding) % 8 * 4;

  for (unsigned int plane = 0; plane < 3; ++plane) {
    uint32_t& color = colorbuf_[word_index + plane * kColorPlaneSizeWords];
    uint32_t bg = (color & (0xcu << bit_index)) >> 2;
    uint32_t fg = (color & (0x3u << bit_index)) << 2;
    color = (color & ~(0xfu << bit_index)) | bg | fg;
  }
}

#undef GRAPHICS_NOFLASH

}  // namespace eight_bit