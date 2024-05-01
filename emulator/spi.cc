#include "spi.h"

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "ioport.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<SPI>> SPI::create(IOPort* port, uint8_t cs_pin,
                                                 uint8_t clk_pin,
                                                 uint8_t mosi_pin,
                                                 uint8_t miso_pin) {
  auto spi =
      std::unique_ptr<SPI>(new SPI(port, cs_pin, clk_pin, mosi_pin, miso_pin));
  auto status = spi->initialize();
  if (!status.ok()) {
    return status;
  }
  return spi;
}

void SPI::set_byte_received_callback(const byte_received_callback& callback) {
  byte_received_callback_ = callback;
}

void SPI::set_chip_select_callback(const chip_select_callback& callback) {
  chip_select_callback_ = callback;
}

SPI::SPI(IOPort* port, uint8_t cs_pin, uint8_t clk_pin, uint8_t mosi_pin,
         uint8_t miso_pin)
    : port_(port),
      cs_mask_(1 << cs_pin),
      clk_mask_(1 << clk_pin),
      mosi_mask_(1 << mosi_pin),
      miso_mask_(1 << miso_pin),
      state_{.previous_cs = cs_mask_} {}

absl::Status SPI::initialize() {
  port_->register_read_callback([this]() { return sub_data_out(); });
  port_->register_write_callback([this](uint8_t data) { sub_data_in(data); });
  return absl::OkStatus();
}

uint8_t SPI::sub_data_out() { return state_.port_data & miso_mask_; }

void SPI::sub_data_in(uint8_t data) {
  // Check for chip select (CS) transitions.
  if ((cs_mask_ & data) != state_.previous_cs) {
    state_.previous_cs = (cs_mask_ & data);
    // SPI mode 0: CS is active low.
    bool to_enabled_transition = (cs_mask_ & data) == 0;
    if (chip_select_callback_) {
      state_.sub_data_out = chip_select_callback_(to_enabled_transition);
    } else {
      state_.sub_data_out = 0xff;
    }
    // We need to present the first bit on CS going low.
    state_.port_data = (state_.sub_data_out & 0x80) ? miso_mask_ : 0;
    state_.bit_count = 0;
    return;
  }
  // Check for clock transitions.
  if ((clk_mask_ & data) != state_.previous_clock) {
    state_.previous_clock = (clk_mask_ & data);
    bool low_to_high = (clk_mask_ & data) > 0;
    // We implment SPI mode 0: sampling happens on the rising clock edge.
    if (low_to_high) {
      state_.sub_data_in = state_.sub_data_in << 1;
      if (mosi_mask_ & data) {
        state_.sub_data_in |= 1;
      }
      state_.bit_count++;
      if (state_.bit_count == 8) {
        if (byte_received_callback_) {
          state_.sub_data_out = byte_received_callback_(state_.sub_data_in);
        } else {
          state_.sub_data_out = 0xff;
        }
        state_.bit_count = 0;
      }
    } else {
      // Falling edge: shift out the next sub data output bit.
      uint8_t out_bit = state_.sub_data_out & 0x80;
      state_.sub_data_out <<= 1;
      if (out_bit) {
        state_.port_data |= miso_mask_;
      } else {
        state_.port_data &= ~miso_mask_;
      }
    }
  }
}

}  // namespace eight_bit