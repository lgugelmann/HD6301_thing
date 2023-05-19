#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/bit_ops.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico/stdlib.h"

extern "C" {
#include "dvi.h"
#include "dvi_serialiser.h"
#include "tmds_encode_font_2bpp.h"
}

#include "font.h"

constexpr int kLedPin = 25;
constexpr int kLedMask = 1 << kLedPin;
// D1..D6 are at pins 14,13...9
constexpr int kD0Pin = 15;
constexpr int kD7Pin = 8;
constexpr int kDMask = 0xff << kD7Pin;
// A1..A4 are at pins 5...2
constexpr int kA0Pin = 6;
constexpr int kA5Pin = 1;
constexpr int kAMask = 0x3f << kA5Pin;
constexpr int kAddressPinCount = 6;
constexpr int kCsPin = 0;
constexpr int kCsMask = 1 << kCsPin;
constexpr int kAllPinMask = kDMask | kAMask | kCsMask | kLedMask;

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

constexpr int kFrameWidth = 800;
constexpr int kFrameHeight = 600;

constexpr int kFontCharWidth = 8;
constexpr int kFontCharHeight = 15;
constexpr int kFontNumChars = 128;

constexpr int kNumCols = kFrameWidth / kFontCharWidth;
constexpr int kNumRows = kFrameHeight / kFontCharHeight;
constexpr int kCharBufSize = kNumRows * kNumCols;

// Color is stored in 3 planes, one each for R, G, B. Within each plane we have
// 4 bits per character, 2 foreground, 2 background. We can fit color for 8
// characters into 32 bit words. We also need to have each line word-aligned so
// we may need some extra padding at the end of each one if the number of
// columns is not divisible by 8.
constexpr int kColorPlaneLineWords = kNumCols / 8 + (kNumCols % 8 > 0);
constexpr int kColorPlaneSizeWords = kColorPlaneLineWords * kNumRows;

struct graphics_state {
  // 3 color planes, one each for R, G, B
  uint32_t colorbuf[3 * kColorPlaneSizeWords] = {0};
  char charbuf[kCharBufSize] = {0};
  int cursor_pos = 0;
  int cursor_pos_high = 0;
  bool cursor_hidden = false;
};

graphics_state* gstate = 0;

static inline void set_color_bits(uint position, uint8_t color,
                                  uint8_t bit_offset) {
  if (position >= kCharBufSize) {
    return;
  }
  // We need to account for some extra padding in the color plane vs the
  // character buffer. With each line we may get an extra word.
  uint line_padding = (position / kNumCols) * (kNumCols % 8);
  uint bit_index = (position + line_padding) % 8 * 4 + bit_offset;
  uint word_index = (position + line_padding) / 8;
  for (int plane = 0; plane < 3; ++plane) {
    uint32_t color_masked = color & 0x3;
    gstate->colorbuf[word_index] =
        (gstate->colorbuf[word_index] & ~(0x3u << bit_index)) |
        (color_masked << bit_index);
    color >>= 2;
    word_index += kColorPlaneSizeWords;
  }
}

static inline void set_fg_color(uint position, uint8_t fg) {
  set_color_bits(position, fg, 0);
}

static inline void set_bg_color(uint position, uint8_t bg) {
  set_color_bits(position, bg, 2);
}

static inline void set_color(uint position, uint8_t fg, uint8_t bg) {
  set_fg_color(position, fg);
  set_bg_color(position, bg);
}

static inline void prepare_scanline(const graphics_state* state, uint y) {
  uint32_t* tmdsbuf;
  queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

  for (int plane = 0; plane < 3; ++plane) {
    tmds_encode_font_2bpp(
        (uint8_t*)&state->charbuf[(y / kFontCharHeight) * kNumCols],
        &state->colorbuf[(y / kFontCharHeight) * kColorPlaneLineWords] +
            plane * kColorPlaneSizeWords,
        tmdsbuf + plane * (kFrameWidth / DVI_SYMBOLS_PER_WORD), kFrameWidth,
        &font[(y % kFontCharHeight) * kFontNumChars]);
  }
  queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash("main") core1_scanline_callback() {
  static uint y = 1;
  prepare_scanline(gstate, y);
  y = (y + 1) % kFrameHeight;
}

void __not_in_flash("main") core1_main() {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  sem_acquire_blocking(&dvi_start_sem);
  dvi_start(&dvi0);

  while (true) {
    __wfi();
  }
  __builtin_unreachable();
}

// Flips fg/bg color at the cursor if needed
void cursor_color_flip(graphics_state* state) {
  if (state->cursor_hidden) {
    return;
  }

  uint line_padding = (state->cursor_pos / kNumCols) * (kNumCols % 8);
  uint word_index = (state->cursor_pos + line_padding) / 8;
  uint bit_index = (state->cursor_pos + line_padding) % 8 * 4;

  for (uint plane = 0; plane < 3; ++plane) {
    uint32_t& color =
        state->colorbuf[word_index + plane * kColorPlaneSizeWords];
    uint32_t bg = (color & (0xcu << bit_index)) >> 2;
    uint32_t fg = (color & (0x3u << bit_index)) << 2;
    color = (color & ~(0xfu << bit_index)) | bg | fg;
  }
}

void run_command(uint8_t command, uint8_t data) {
  switch (command) {
    // Write character, advance cursor
    case 0: {
      gstate->charbuf[gstate->cursor_pos] = data & 0x7f;
      cursor_color_flip(gstate);
      gstate->cursor_pos = (gstate->cursor_pos + 1) % kCharBufSize;
      cursor_color_flip(gstate);
      break;
    }
    // Clear commands
    case 1: {
      switch (data) {
        case 0: {  // Clear full screen, reset cursor to 0, colors to white on
                   // black
          memset(gstate->charbuf, ' ', kCharBufSize);
          memset(gstate->colorbuf, 0x33,
                 3 * kColorPlaneSizeWords * sizeof(uint32_t));
          gstate->cursor_pos = 0;
          cursor_color_flip(gstate);
          break;
        }
        case 1: {  // Clear current row, reset cursor to row start
          gstate->cursor_pos -= (gstate->cursor_pos % kNumCols);
          memset(gstate->charbuf + gstate->cursor_pos, ' ', kNumCols);
          for (int position = gstate->cursor_pos;
               position < gstate->cursor_pos + kNumCols; ++position) {
            set_color(position, 0xff, 0);
          }
          cursor_color_flip(gstate);
          break;
        }
        case 2: {  // Clear next row, reset cursor to next row start
          cursor_color_flip(gstate);
          gstate->cursor_pos = (gstate->cursor_pos -
                                (gstate->cursor_pos % kNumCols) + kNumCols) %
                               kCharBufSize;
          memset(gstate->charbuf + gstate->cursor_pos, ' ', kNumCols);
          for (int position = gstate->cursor_pos;
               position < gstate->cursor_pos + kNumCols; ++position) {
            set_color(position, 0xff, 0);
          }
          cursor_color_flip(gstate);
          break;
        }
      }
      break;
    }
    // Cursor position delta commands. Data contains signed delta to cursor
    // position.
    case 2: {
      int new_position =
          (gstate->cursor_pos + (signed char)data) % kCharBufSize;
      if (new_position < 0) {
        new_position += kCharBufSize;
      }
      cursor_color_flip(gstate);
      gstate->cursor_pos = new_position;
      cursor_color_flip(gstate);
      break;
    }
    // Same as 0 but doesn't advance cursor
    case 3: {
      gstate->charbuf[gstate->cursor_pos] = data & 0x7f;
      break;
    }
    // Set cursor column
    case 4: {
      cursor_color_flip(gstate);
      gstate->cursor_pos = gstate->cursor_pos -
                           (gstate->cursor_pos % kNumCols) + data % kNumCols;
      cursor_color_flip(gstate);
      break;
    }
    // Set cursor row
    case 5: {
      cursor_color_flip(gstate);
      gstate->cursor_pos =
          (data % kNumRows) * kNumCols + (gstate->cursor_pos % kNumCols);
      cursor_color_flip(gstate);
      break;
    }
    // Set cursor position high byte
    case 6: {
      gstate->cursor_pos_high = data;
      break;
    }
    // Set cursor position low byte
    case 7: {
      cursor_color_flip(gstate);
      gstate->cursor_pos = (gstate->cursor_pos_high << 8) + data;
      cursor_color_flip(gstate);
      break;
    }
    // Cursor visibility. 0: shown, 1: hidden
    case 8: {
      // If the hidden status changed only one of the flips will be done, if it
      // hasn't we flip 0 or two times. In all cases we get the right result.
      cursor_color_flip(gstate);
      gstate->cursor_hidden = data;
      cursor_color_flip(gstate);
      break;
    }
    default:
      break;
  }
}

void bus_irq_callback(uint gpio, uint32_t event_mask) {
  uint32_t gpio_state = gpio_get_all();
  // Data and address are laid out backside-front on the board
  uint8_t data = __rev((gpio_state & kDMask) >> kD7Pin) >> 24;
  uint8_t command = __rev((gpio_state & kAMask) >> kA5Pin) >> 26;
  run_command(command, data);
}

int __not_in_flash("main") main() {
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  sleep_ms(10);

  // Run system at TMDS bit clock
  const auto* dvi_timing = &dvi_timing_800x600p_60hz;
  set_sys_clock_khz(dvi_timing->bit_clk_khz, true);

  stdio_init_all();

  gstate = new graphics_state;

  printf("rows: %u, cols: %u charbuf_size: %d\n", kNumRows, kNumCols,
         kCharBufSize);

  // Clears the screen
  run_command(1, 0);

  // Bus I/O & IRQ init
  gpio_init_mask(kAllPinMask);  // Sets pins in the mask to input
  gpio_pull_up(kCsPin);
  for (int i = kA5Pin; i <= kA0Pin; ++i) {
    gpio_pull_down(i);
  }
  gpio_set_irq_enabled_with_callback(kCsPin, GPIO_IRQ_EDGE_FALL, true,
                                     &bus_irq_callback);
  gpio_set_dir(kLedPin, true /* set as output */);
  gpio_put(kLedPin, 1);

  // DVI Initialization
  const dvi_serialiser_cfg serializer_cfg = {.pio = pio0,
                                             .sm_tmds = {0, 1, 2},
                                             .pins_tmds = {18, 20, 26},
                                             .pins_clk = 16,
                                             .invert_diffpairs = true};
  dvi0.timing = dvi_timing;
  dvi0.ser_cfg = serializer_cfg;
  dvi0.scanline_callback = core1_scanline_callback;

  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

  prepare_scanline(gstate, 0);

  sem_init(&dvi_start_sem, 0, 1);
  hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
  multicore_launch_core1(core1_main);
  sem_release(&dvi_start_sem);

  // Sit around waiting for interrupts
  while (true) {
    __wfi();
  }

  __builtin_unreachable();
}
