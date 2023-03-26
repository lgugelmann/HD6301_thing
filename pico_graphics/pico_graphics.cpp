#include <cstdio>
#include <cstdlib>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico/stdlib.h"

extern "C" {
#include "dvi.h"
#include "dvi_serialiser.h"
#include "tmds_encode.h"
}

#include "font.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 15
#define FONT_N_CHARS 128
#define FONT_FIRST_ASCII 0

constexpr int kD0Pin = 0;
constexpr int kDMask = 0xff;
constexpr int kA0Pin = 8;
constexpr int kAMask = 3 << kA0Pin;
constexpr int kAddressPinCount = 3;
constexpr int kCsPin = 11;
constexpr int kCsMask = 1 << kCsPin;
constexpr int kAllPinMask = kDMask | kAMask | kCsMask;

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

constexpr int kFrameWidth = 800;
constexpr int kFrameHeight = 600;

constexpr int kNumCols = kFrameWidth / FONT_CHAR_WIDTH;
constexpr int kNumRows = kFrameHeight / FONT_CHAR_HEIGHT;
constexpr int kCharBufSize = kNumRows * kNumCols;
char charbuf[kCharBufSize] = {0};

static inline void prepare_scanline(const char *chars, uint y) {
  static uint8_t scanbuf[kFrameWidth / 8];
  // First blit font into 1bpp scanline buffer, then encode scanbuf into tmdsbuf
  for (uint i = 0; i < kNumCols; ++i) {
    uint c = chars[i + y / FONT_CHAR_HEIGHT * kNumCols];
    scanbuf[i] =
        font[(c - FONT_FIRST_ASCII) + (y % FONT_CHAR_HEIGHT) * FONT_N_CHARS];
  }
  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
  tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, kFrameWidth);
  queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
}

void core1_scanline_callback() {
  static uint y = 1;
  prepare_scanline(charbuf, y);
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
  static int cursor_row = 0;
  static int cursor_column = 0;
  uint32_t gpio_state = gpio_get_all();
  char data = (gpio_state & kDMask) >> kD0Pin;
  uint32_t command = (gpio_state & kAMask) >> kA0Pin;
  switch (command) {
    case 0: {
      // make sure we don't accidentally write bytes >127
      int pos = cursor_column + cursor_row * kNumCols;
      charbuf[pos] = data & 0x7f;
      charbuf[(pos + 1) % kCharBufSize] = '_';
      cursor_column = cursor_column + 1;
      if (cursor_column == kNumCols) {
        cursor_column = 0;
        cursor_row = (cursor_row + 1) % kNumRows;
      }
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
    charbuf[i] = ' ';
  }
  charbuf[0] = '_';

  // Bus I/O & IRQ init
  gpio_init_mask(kAllPinMask);  // Sets pins in the mask to input
  gpio_pull_up(kCsPin);
  for (int i = kA0Pin; i < kA0Pin + kAddressPinCount; ++i) {
    gpio_pull_down(i);
  }
  gpio_set_irq_enabled_with_callback(kCsPin, GPIO_IRQ_EDGE_FALL, true,
                                     &bus_irq_callback);

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

  prepare_scanline(charbuf, 0);

  sem_init(&dvi_start_sem, 0, 1);
  hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
  multicore_launch_core1(core1_main);
  sem_release(&dvi_start_sem);

  printf("init done");
  // Sit around waiting for interrupts
  while (true) {
    __wfi();
  }

  __builtin_unreachable();
}
