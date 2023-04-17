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
#include "tmds_encode.h"
}

#include "font.h"

// Note: width has to be 8 or you need to change the logic in prepare_scanline
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 15
#define FONT_N_CHARS 128
#define FONT_FIRST_ASCII 0

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

constexpr int kNumCols = kFrameWidth / FONT_CHAR_WIDTH;
constexpr int kNumRows = kFrameHeight / FONT_CHAR_HEIGHT;
constexpr int kCharBufSize = kNumRows * kNumCols;

struct graphics_state {
  char charbuf[kCharBufSize] = {0};
  int cursor_pos = 0;
};

graphics_state state;

static inline void prepare_scanline(const graphics_state *state, uint y) {
  static uint8_t scanbuf[kNumCols];
  // First blit font into 1bpp scanline buffer, then encode scanbuf into tmdsbuf
  for (uint i = 0; i < kNumCols; ++i) {
    uint c = state->charbuf[i + y / FONT_CHAR_HEIGHT * kNumCols];
    scanbuf[i] =
        font[(c - FONT_FIRST_ASCII) + (y % FONT_CHAR_HEIGHT) * FONT_N_CHARS];
  }
  uint cursor_pixel_row = (state->cursor_pos / kNumCols) * FONT_CHAR_HEIGHT;
  if (cursor_pixel_row <= y && y < cursor_pixel_row + FONT_CHAR_HEIGHT) {
    // we're in the cursor row, invert the character under the cursor
    scanbuf[state->cursor_pos % kNumCols] =
        ~scanbuf[state->cursor_pos % kNumCols];
  }
  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
  tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, kFrameWidth);
  queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash("main") core1_scanline_callback() {
  static uint y = 1;
  prepare_scanline(&state, y);
  y = (y + 1) % kFrameHeight;
}

void __not_in_flash("main") core1_main() {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  sem_acquire_blocking(&dvi_start_sem);
  dvi_start(&dvi0);

  // The text display is completely IRQ driven (takes up around 30% of cycles @
  // VGA). We could do something useful, or we could just take a nice nap
  while (true) {
    __wfi();
  }
  __builtin_unreachable();
}

void bus_irq_callback(uint gpio, uint32_t event_mask) {
  uint32_t gpio_state = gpio_get_all();
  // Data and address are laid out backside-front on the board
  uint8_t data = __rev((gpio_state & kDMask) >> kD7Pin) >> 24;
  uint8_t command = __rev((gpio_state & kAMask) >> kA5Pin) >> 26;
  switch (command) {
    // Write character, advance cursor
    case 0: {
      state.charbuf[state.cursor_pos] = data & 0x7f;
      state.cursor_pos = (state.cursor_pos + 1) % kCharBufSize;
      break;
    }
    // Clear commands
    case 1: {
      switch (data) {
        case 0: {  // Clear full screen, reset cursor to 0
          memset(state.charbuf, ' ', kCharBufSize);
          state.cursor_pos = 0;
          break;
        }
        case 1: {  // Clear current row, reset cursor to row start
          state.cursor_pos -= (state.cursor_pos % kNumCols);
          memset(state.charbuf + state.cursor_pos, ' ', kNumCols);
          break;
        }
        case 2: {  // Clear next row, reset cursor to next row start
          state.cursor_pos =
              (state.cursor_pos - (state.cursor_pos % kNumCols) + kNumCols) %
              kCharBufSize;
          memset(state.charbuf + state.cursor_pos, ' ', kNumCols);
          break;
        }
      }
      break;
    }
    // Cursor position delta commands. Data contains signed delta to cursor
    // position.
    case 2: {
      int new_position = (state.cursor_pos + (signed char)data) % kCharBufSize;
      if (new_position < 0) {
        new_position += kCharBufSize;
      }
      state.cursor_pos = new_position;
      break;
    }
    // Same as 0 but doesn't advance cursor
    case 3: {
      state.charbuf[state.cursor_pos] = data & 0x7f;
      break;
    }

    default:
      break;
  }
}

int __not_in_flash("main") main() {
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  sleep_ms(10);

  // Run system at TMDS bit clock
  const auto *dvi_timing = &dvi_timing_800x600p_60hz;
  set_sys_clock_khz(dvi_timing->bit_clk_khz, true);

  stdio_init_all();

  // Fill character buffer with space characters
  for (int i = 0; i < kNumRows * kNumCols; ++i) {
    state.charbuf[i] = ' ';
  }

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

  prepare_scanline(&state, 0);

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
