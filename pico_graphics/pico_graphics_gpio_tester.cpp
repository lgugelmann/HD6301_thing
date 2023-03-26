#include <cstdio>

#include "pico/stdlib.h"

constexpr int kD0Pin = 0;
constexpr int kDMask = 0xff;
constexpr int kA0Pin = 8;
constexpr int kAMask = 3 << kA0Pin;
constexpr int kAddressPinCount = 3;
constexpr int kCsPin = 11;
constexpr int kCsMask = 1 << kCsPin;
constexpr int kAllPinMask = kDMask | kAMask | kCsMask;

int main() {
  stdio_init_all();

  gpio_init_mask(kAllPinMask);
  gpio_clr_mask(kDMask | kAMask);
  gpio_put(kCsPin, 1);
  gpio_set_dir_out_masked(kAllPinMask);

  while (true) {
    uint32_t c = getchar();
    putchar(c);
    gpio_put_masked(kDMask, c << kD0Pin);
    gpio_put(kCsPin, 0);
    sleep_ms(1);
    gpio_put(kCsPin, 1);
  }
}
