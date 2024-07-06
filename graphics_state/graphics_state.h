#ifndef EIGHT_BIT_GRAPHICS_STATE_H
#define EIGHT_BIT_GRAPHICS_STATE_H

#include <cstdint>
#include <cstring>

namespace eight_bit {

inline constexpr int kFrameWidth = 800;
inline constexpr int kFrameHeight = 600;

inline constexpr int kFontCharWidth = 8;
inline constexpr int kFontCharHeight = 15;
inline constexpr int kFontNumChars = 128;

inline constexpr int kNumColumns = kFrameWidth / kFontCharWidth;
inline constexpr int kNumRows = kFrameHeight / kFontCharHeight;
inline constexpr int kCharBufSize = kNumRows * kNumColumns;

// Color is stored in 3 planes, one each for R, G, B. Within each plane we
// have 4 bits per character, 2 foreground, 2 background. We can fit color for
// 8 characters into 32 bit words. We also need to have each line word-aligned
// so we may need some extra padding at the end of each one if the number of
// columns is not divisible by 8.
inline constexpr int kColorPlaneLineWords =
    kNumColumns / 8 + (kNumColumns % 8 > 0);
inline constexpr int kColorPlaneSizeWords = kColorPlaneLineWords * kNumRows;

class GraphicsState {
  static_assert(sizeof(uint32_t) == 4,
                "Expecting uint32_t to be exactly 32-bit, not just 'at least' "
                "as per the standard");

 public:
  GraphicsState() = default;
  ~GraphicsState() = default;

  void HandleCommand(uint8_t command, uint8_t data);
  const char* GetCharBuf() const { return charbuf_; }
  const uint32_t* GetColorBuf() const { return colorbuf_; }

  uint8_t GetBackgroundColor(int position) const;
  uint8_t GetForegroundColor(int position) const;
  int GetRowRoll() const { return row_roll_; }

 protected:
  void SetFgColor(unsigned int position, uint8_t fg);
  void SetBgColor(unsigned int position, uint8_t bg);
  void SetColor(unsigned int position, uint8_t fg, uint8_t bg);

 private:
  void SetColorBits(unsigned int position, uint8_t color, uint8_t bit_offset);
  void CursorColorFlip();

  // 3 color planes, one each for B, G, R
  uint32_t colorbuf_[3 * kColorPlaneSizeWords] = {0};
  char charbuf_[kCharBufSize] = {0};
  int cursor_pos_ = 0;
  int cursor_pos_high_ = 0;
  bool cursor_hidden_ = false;
  int row_roll_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_GRAPHICS_STATE_H
