#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "font.h"
#include "graphics_state.h"
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

using eight_bit::GraphicsState;
using eight_bit::kCharBufSize;
using eight_bit::kColorPlaneLineWords;
using eight_bit::kColorPlaneSizeWords;
using eight_bit::kFontCharHeight;
using eight_bit::kFontNumChars;
using eight_bit::kFrameHeight;
using eight_bit::kFrameWidth;
using eight_bit::kNumColumns;
using eight_bit::kNumRows;

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

GraphicsState* gstate = nullptr;

static void __not_in_flash("main") core1_scanline_callback() {
  static uint screen_y = 0;
  static int roll_y = 0;
  // Only update roll at the beginning of the frame to avoid weird artifacts
  if (screen_y == 0) {
    roll_y = gstate->GetRowRoll() * kFontCharHeight;
  }

  uint logical_y = (screen_y - roll_y + kFrameHeight) % kFrameHeight;

  uint32_t* tmdsbuf;
  queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

  const char* charbuf = gstate->GetCharBuf();
  const uint32_t* colorbuf = gstate->GetColorBuf();

  for (int plane = 0; plane < 3; ++plane) {
    tmds_encode_font_2bpp(
        (uint8_t*)&charbuf[(logical_y / kFontCharHeight) * kNumColumns],
        &colorbuf[(logical_y / kFontCharHeight) * kColorPlaneLineWords] +
            plane * kColorPlaneSizeWords,
        tmdsbuf + plane * (kFrameWidth / DVI_SYMBOLS_PER_WORD), kFrameWidth,
        &font[(logical_y % kFontCharHeight) * kFontNumChars]);
  }
  queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);

  screen_y = (screen_y + 1) % kFrameHeight;
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

void __not_in_flash("main") bus_irq_callback(uint gpio, uint32_t event_mask) {
  uint32_t gpio_state = gpio_get_all();
  // Data and address are laid out backside-front on the board
  uint8_t data = __rev((gpio_state & kDMask) >> kD7Pin) >> 24;
  uint8_t command = __rev((gpio_state & kAMask) >> kA5Pin) >> 26;
  gstate->HandleCommand(command, data);
}

int __not_in_flash("main") main() {
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  sleep_ms(10);

  // Run system at TMDS bit clock
  const auto* dvi_timing = &dvi_timing_800x600p_60hz;
  set_sys_clock_khz(dvi_timing->bit_clk_khz, true);

  stdio_init_all();

  gstate = new GraphicsState();
  gstate->HandleCommand(1, 0);  // Clears the screen

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

  // Populate the first scanline
  core1_scanline_callback();

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
