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

const uint IRQ_PIN = 2;
const uint C1_PIN = 3;
const uint CLK_PIN = 4;
const uint RW_PIN = 5;
const uint STBY_PIN = 26;

const uint ADDR_H_LATCH_PIN = 6;
const uint ADDR_H_LATCH_OE_PIN = 7;
const uint DATA_LATCH_OE_PIN = 16;
const uint DATA_LATCH_PIN = 17;
const uint DATA_BUF_OE_PIN = 18;
const uint ADDR_L_BUF_OE_PIN = 19;
const uint ADDR_L_LATCH_PIN = 20;
const uint ADDR_L_LATCH_OE_PIN = 21;
const uint ADDR_H_BUF_OE_PIN = 22;

// D0...D7 are assumed contiguous in pin numbers, so D0=8...D7=15
const uint DATA_PIN_0 = 8;

const uint LED_PIN = 25;

const uint PIN_MASK_DATA = 0xff << DATA_PIN_0;

const uint PIN_MASK_LATCH_OE = 1 << DATA_LATCH_OE_PIN |
                               1 << ADDR_L_LATCH_OE_PIN |
                               1 << ADDR_H_LATCH_OE_PIN;

const uint PIN_MASK_BUF_OE =
    1 << DATA_BUF_OE_PIN | 1 << ADDR_L_BUF_OE_PIN | 1 << ADDR_H_BUF_OE_PIN;

const uint PIN_MASK_OE = PIN_MASK_LATCH_OE | PIN_MASK_BUF_OE;

const uint PIN_MASK_LATCH =
    1 << DATA_LATCH_PIN | 1 << ADDR_L_LATCH_PIN | 1 << ADDR_H_LATCH_PIN;



const uint PIN_MASK_BUS = 1 << CLK_PIN | 1 << RW_PIN;

const uint PIN_MASK_LED = 1 << LED_PIN;
const uint PIN_MASK_STBY = 1 << STBY_PIN;
const uint PIN_MASK_C1 = 1 << C1_PIN;
const uint PIN_MASK_IRQ = 1 << IRQ_PIN;

const uint PIN_MASK_ALL = PIN_MASK_DATA | PIN_MASK_OE | PIN_MASK_LATCH |
                          PIN_MASK_BUS | PIN_MASK_LED | PIN_MASK_STBY |
                          PIN_MASK_C1 | PIN_MASK_IRQ;

void enter_stby() {
  gpio_put(LED_PIN, 1);
  // Make sure input buffers aren't driving pico D* pins just in case.
  gpio_set_mask(PIN_MASK_BUF_OE);
  // Enter standby.
  gpio_put(STBY_PIN, 0);
  // Data, address latches drive system bus
  gpio_clr_mask(PIN_MASK_LATCH_OE);
}

void exit_stby() {
  gpio_put(LED_PIN, 0);
  // Stop driving bus from pico.
  gpio_set_mask(PIN_MASK_LATCH_OE);
  gpio_put(STBY_PIN, 1);
}

// Turn 8 data bits into a pin bitfield setting the right D* pin values.
uint data_to_mask(uint8_t bits) { return (uint)bits << DATA_PIN_0; }

// Turn a pin bitfield into data bits
uint8_t mask_to_data(uint bitfield) { return (uint8_t)(bitfield >> DATA_PIN_0); }

void set_latch(uint8_t data, uint latch_pin) {
  gpio_put_masked(PIN_MASK_DATA, data_to_mask(data));
  gpio_put(latch_pin, 1);
  busy_wait_us_32(1);
  gpio_put(latch_pin, 0);
}

void set_data(uint8_t data) {
  set_latch(data, DATA_LATCH_PIN);
}

void set_address_low(uint8_t data) {
  set_latch(data, ADDR_L_LATCH_PIN);
}

void set_address_high(uint8_t data) {
  set_latch(data, ADDR_H_LATCH_PIN);
}

void set_address(uint16_t address) {
  set_address_low(address & 0xff);
  set_address_high(address >> 8);
}

uint8_t read_cycle() {
  // Set RW to read
  gpio_put(RW_PIN, 1);
  busy_wait_us_32(1);

  // Pico data pins to input
  gpio_set_dir_in_masked(PIN_MASK_DATA);
  // Disable driving bus data pins from data latch
  gpio_put(DATA_LATCH_OE_PIN, 1);
  // Enable reading from bus data and writing to Pico data pins
  gpio_put(DATA_BUF_OE_PIN, 0);
  // Clock pin high: OE on RAM/ROM gets enabled
  gpio_put(CLK_PIN, 1);
  busy_wait_us_32(1);
  uint8_t data = mask_to_data(gpio_get_all());

  // The same procedure in reverse
  gpio_put(CLK_PIN, 0);
  gpio_put(DATA_BUF_OE_PIN, 1);
  gpio_put(DATA_LATCH_OE_PIN, 0);
  gpio_set_dir_out_masked(PIN_MASK_DATA);

  return data;
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
  gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);

  // Set to input, clear any output value
  gpio_init_mask(PIN_MASK_ALL);

  // Set every OE pin, STBY, RW high, all others stay clear
  gpio_set_mask(PIN_MASK_OE | PIN_MASK_STBY | 1 << RW_PIN);

  // PIN_MASK_C1 | PIN_MASK_IRQ stay as input. CLK and RW are put into high
  // impedance by the GreenPAK chip depending on STBY state - we can always
  // drive them here.
  gpio_set_dir_out_masked(PIN_MASK_DATA | PIN_MASK_OE | PIN_MASK_LATCH |
                          PIN_MASK_LED | PIN_MASK_STBY | PIN_MASK_BUS);

  // Set STBY, LED values
  exit_stby();
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

  tusb_init();
  // pico_stdio takes over the first CDC
  stdio_init_all();
  uart_init(uart0, 62500);

  init_pins();

  while (true) {
    tud_task();
    cdc_to_uart_task();
    uart_to_cdc_task();
    programmer_task();
  }
  return 0;
}
