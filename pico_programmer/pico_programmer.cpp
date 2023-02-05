#include <cstdint>
#include <cstdio>

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include "tusb.h"

const uint8_t PROGRAM_CDC = 0;
const uint8_t UART_CDC = 1;

const uint UART0_TX_PIN = 0;
const uint UART0_RX_PIN = 1;

// D0...D7 are assumed contiguous in pin numbers, so D0 = 2...D7=9
const uint DATA_PIN_0 = 2;

const uint DATA_LATCH_PIN = 10;
const uint ADDR_L_LATCH_PIN = 11;
const uint ADDR_H_LATCH_PIN = 12;
const uint STBY_PIN = 13;

const uint AS_PIN = 14;
const uint RW_PIN = 15;
const uint CLK_PIN = 16;

const uint DATA_IN_PINS[] = {17, 18, 19, 20, 21, 22, 26, 27};

const uint LED_PIN = 25;

constexpr uint data_out_mask(uint8_t bits) { return (uint)bits << DATA_PIN_0; }
const uint PIN_MASK_DATA_OUT = data_out_mask(0xff);

constexpr uint pin_mask_out() {
  uint mask = 0;
  mask |= 1 << DATA_LATCH_PIN;
  mask |= 1 << ADDR_L_LATCH_PIN;
  mask |= 1 << ADDR_H_LATCH_PIN;
  mask |= 1 << STBY_PIN;
  mask |= 1 << AS_PIN;
  mask |= 1 << RW_PIN;
  mask |= 1 << CLK_PIN;
  mask |= 1 << LED_PIN;
  mask |= data_out_mask(0xff);
  return mask;
}
constexpr uint PIN_MASK_OUT = pin_mask_out();

constexpr uint pin_mask_in() {
  uint ret = 0;
  for (uint i = 0; i < 8; ++i) {
    ret |= 1 << DATA_IN_PINS[i];
  }
  return ret;
}
constexpr uint PIN_MASK_IN = pin_mask_in();

void enter_stby() {
  gpio_put(LED_PIN, 1);
  gpio_put(STBY_PIN, 0);
}

void exit_stby() {
  gpio_put(LED_PIN, 0);
  gpio_put(STBY_PIN, 1);
}

void strobe_as() {
  gpio_put(AS_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(AS_PIN, 0);
}

void set_data(const uint8_t data) {
  gpio_put_masked(PIN_MASK_DATA_OUT, data_out_mask(data));
  gpio_put(DATA_LATCH_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(DATA_LATCH_PIN, 0);
}

void set_address_low(const uint8_t address) {
  gpio_put_masked(PIN_MASK_DATA_OUT, data_out_mask(address));
  gpio_put(DATA_LATCH_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(DATA_LATCH_PIN, 0);
  strobe_as();
}

void set_address_high(const uint8_t address) {
  gpio_put_masked(PIN_MASK_DATA_OUT, data_out_mask(address));
  gpio_put(ADDR_H_LATCH_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(ADDR_H_LATCH_PIN, 0);
}

void set_address(uint16_t address) {
  set_address_low(address & 0xff);
  set_address_high(address >> 8);
}

uint8_t read_cycle() {
  gpio_put(RW_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(CLK_PIN, 1);
  busy_wait_us_32(1);
  int read = gpio_get_all();
  gpio_put(CLK_PIN, 0);

  uint8_t ret = 0;
  for (int i = 0; i < 8; ++i) {
    ret += ((read >> DATA_IN_PINS[i]) & 1) << i;
  }
  return ret;
}

void write_cycle() {
  gpio_put(RW_PIN, 0);
  busy_wait_us_32(1);
  gpio_put(CLK_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(CLK_PIN, 0);
  gpio_put(RW_PIN, 1);
  busy_wait_us_32(20);
}

void read_data(uint16_t start, uint16_t end) {
  for (uint16_t i = start; i != (uint16_t)(end + 1); ++i) {
    set_address(i);
    putchar(read_cycle());
  }
}

void init_pins() {
  // Set STBY, LED
  exit_stby();

  gpio_put(DATA_LATCH_PIN, 0);
  gpio_put(ADDR_L_LATCH_PIN, 0);
  gpio_put(ADDR_H_LATCH_PIN, 0);

  gpio_put(AS_PIN, 0);
  gpio_put(RW_PIN, 1);
  gpio_put(CLK_PIN, 0);
}

void cdc_to_uart_task() {
  if (tud_cdc_n_available(UART_CDC)) {
    uint8_t buf[64];
    uint32_t count = tud_cdc_n_read(UART_CDC, buf, sizeof(buf));
    uart_write_blocking(uart0, buf, count);
  }
}

void uart_to_cdc_task() {
  uint8_t count = 0;
  while (uart_is_readable(uart0) && count < 32) {
    char c = uart_getc(uart0);
    tud_cdc_n_write_char(UART_CDC, c);
    count += 1;
  }
  tud_cdc_n_write_flush(UART_CDC);
}

void programmer_task() {
  int c = getchar_timeout_us(1);
  if (c == PICO_ERROR_TIMEOUT) {
    return;
  }
  switch (c) {
    case 's':
      enter_stby();
      break;
    case 'x':
      exit_stby();
      break;
    case 'd':
      // Set data bits
      c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        putchar('e');
        break;
      }
      set_data(c);
      break;
    case 'l':
      // Set low address bits
      c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        putchar('e');
        break;
      }
      set_address_low(c);
      break;
    case 'h':
      // Set high address bits
      c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        putchar('e');
        break;
      }
      set_address_high(c);
      break;
    case 'a':
      strobe_as();
      break;
    case 'w':
      write_cycle();
      break;
    case 'r': {
      int addr_start_l = getchar_timeout_us(10000);
      int addr_start_h = getchar_timeout_us(10000);
      int addr_end_l = getchar_timeout_us(10000);
      int addr_end_h = getchar_timeout_us(10000);
      if (addr_start_l == PICO_ERROR_TIMEOUT ||
          addr_start_h == PICO_ERROR_TIMEOUT ||
          addr_end_l == PICO_ERROR_TIMEOUT ||
          addr_end_h == PICO_ERROR_TIMEOUT) {
        putchar('e');
        break;
      } else {
        putchar('r');
      }
      uint16_t addr_start = addr_start_l + (addr_start_h << 8);
      uint16_t addr_end = addr_end_l + (addr_end_h << 8);
      read_data(addr_start, addr_end);
      break;
    }
    default:
      break;
  }
}

int main(int argc, char* argv[]) {
  bi_decl(bi_program_description("HD6301 programmer."));
  bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED"));

  tusb_init();
  // pico_stdio takes over the first CDC
  stdio_init_all();
  uart_init(uart0, 62500);

  gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);
  gpio_init_mask(PIN_MASK_OUT | PIN_MASK_IN);
  gpio_set_dir_out_masked(PIN_MASK_OUT);
  gpio_set_dir_in_masked(PIN_MASK_IN);

  init_pins();

  while (true) {
    tud_task();
    cdc_to_uart_task();
    uart_to_cdc_task();
    programmer_task();
  }
  return 0;
}
