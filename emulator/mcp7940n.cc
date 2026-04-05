#include "mcp7940n.h"

#include <chrono>
#include <ctime>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace eight_bit {

namespace {
uint8_t to_bcd(int value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}
}  // namespace

std::unique_ptr<MCP7940N> MCP7940N::create(IOPort* port_a) {
  return std::unique_ptr<MCP7940N>(new MCP7940N(port_a));
}

MCP7940N::MCP7940N(IOPort* port_a) : port_a_(port_a) {
  port_a_->register_ddr_change_callback(
      [this](uint8_t ddr) { on_ddr_change(ddr); });
}

void MCP7940N::on_ddr_change(uint8_t ddr) {
  const bool new_scl = !(ddr & kSCLMask);
  const bool new_sda = !(ddr & kSDAMask);

  if (new_scl != scl_) {
    scl_ = new_scl;
    sda_ = new_sda;
    if (new_scl) {
      on_scl_rising();
    } else {
      on_scl_falling();
    }
  } else if (new_sda != sda_) {
    sda_ = new_sda;
    if (scl_) {
      if (!new_sda) {
        on_start();
      } else {
        on_stop();
      }
    }
    // SDA change while SCL is low: data setup, ignored.
  }
}

void MCP7940N::on_start() {
  VLOG(2) << "MCP7940N: START";
  state_ = State::kReceivingAddr;
  bit_count_ = 0;
  current_byte_ = 0;
}

void MCP7940N::on_stop() {
  VLOG(2) << "MCP7940N: STOP";
  state_ = State::kIdle;
}

void MCP7940N::on_scl_falling() {
  switch (state_) {
    case State::kReceivingAddr:
    case State::kReceivingReg:
    case State::kReceivingData:
      if (bit_count_ == 8) {
        // Byte complete — ACK it by driving SDA low.
        port_a_->provide_inputs(0, kSDAMask);
        acking_prev_state_ = state_;
        state_ = State::kAcking;
      }
      break;

    case State::kAcking: {
      // ACK was clocked; release SDA and transition to the next state.
      port_a_->provide_inputs(kSDAMask, kSDAMask);
      if (acking_prev_state_ == State::kReceivingAddr) {
        if (is_read_) {
          state_ = State::kSendingData;
          send_byte_ = read_register(reg_pointer_);
          bit_count_ = 0;
          drive_sda_bit();  // prepare bit 7 before the first SCL rising
        } else {
          state_ = State::kReceivingReg;
          bit_count_ = 0;
          current_byte_ = 0;
        }
      } else if (acking_prev_state_ == State::kReceivingReg) {
        state_ = State::kReceivingData;
        bit_count_ = 0;
        current_byte_ = 0;
      } else if (acking_prev_state_ == State::kReceivingData) {
        // Continue accepting data bytes (multi-byte write).
        state_ = State::kReceivingData;
        bit_count_ = 0;
        current_byte_ = 0;
      }
      break;
    }

    case State::kSendingData:
      drive_sda_bit();
      break;

    case State::kMasterAcking:
      // Release SDA so the master can drive ACK or NACK.
      port_a_->provide_inputs(kSDAMask, kSDAMask);
      break;

    case State::kIdle:
      break;
  }
}

void MCP7940N::on_scl_rising() {
  switch (state_) {
    case State::kReceivingAddr:
    case State::kReceivingReg:
    case State::kReceivingData:
      current_byte_ = static_cast<uint8_t>((current_byte_ << 1) | (sda_ ? 1 : 0));
      ++bit_count_;
      break;

    case State::kAcking:
      // Process the completed byte now that the ACK clock edge has arrived.
      if (acking_prev_state_ == State::kReceivingAddr) {
        is_read_ = (current_byte_ & 1) != 0;
        VLOG(2) << absl::StreamFormat("MCP7940N: addr byte 0x%02X (%s)",
                                      current_byte_,
                                      is_read_ ? "READ" : "WRITE");
      } else if (acking_prev_state_ == State::kReceivingReg) {
        reg_pointer_ = current_byte_;
        VLOG(2) << absl::StreamFormat("MCP7940N: reg pointer = 0x%02X",
                                      reg_pointer_);
      } else if (acking_prev_state_ == State::kReceivingData) {
        write_register(reg_pointer_, current_byte_);
        ++reg_pointer_;
      }
      break;

    case State::kSendingData:
      ++bit_count_;
      if (bit_count_ == 8) {
        // All 8 bits sent.  Keep driving SDA until the next falling edge
        // (the master's ACK/NACK clock) so the master can still sample the
        // LSB; we release SDA there in on_scl_falling / kMasterAcking.
        state_ = State::kMasterAcking;
      }
      break;

    case State::kMasterAcking:
      if (sda_) {
        // NACK: master is done reading.
        VLOG(2) << "MCP7940N: NACK from master";
        state_ = State::kIdle;
      } else {
        // ACK: advance to the next register and keep sending.
        VLOG(2) << "MCP7940N: ACK from master";
        ++reg_pointer_;
        send_byte_ = read_register(reg_pointer_);
        bit_count_ = 0;
        state_ = State::kSendingData;
        // First bit is driven on the next SCL falling edge.
      }
      break;

    case State::kIdle:
      break;
  }
}

void MCP7940N::drive_sda_bit() {
  const bool bit = ((send_byte_ >> (7 - bit_count_)) & 1) != 0;
  port_a_->provide_inputs(bit ? kSDAMask : 0, kSDAMask);
}

uint8_t MCP7940N::read_register(uint8_t reg) const {
  if (reg >= 0x07) {
    return (reg < sizeof(registers_)) ? registers_[reg] : 0;
  }
  // Time registers 0x00–0x06: always return live system time.
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const std::tm* tm = std::localtime(&t);
  switch (reg) {
    case 0x00:  // RTCSEC: ST=1 | BCD seconds
      return 0x80 | to_bcd(tm->tm_sec);
    case 0x01:  // RTCMIN: BCD minutes
      return to_bcd(tm->tm_min);
    case 0x02:  // RTCHOUR: 24-hour mode, BCD hours
      return to_bcd(tm->tm_hour);
    case 0x03:  // RTCWKDAY: OSCRUN=1, VBATEN=1, day-of-week (1–7)
      return static_cast<uint8_t>(0x28 | (tm->tm_wday + 1));
    case 0x04:  // RTCDATE: BCD day-of-month
      return to_bcd(tm->tm_mday);
    case 0x05:  // RTCMTH: BCD month (tm_mon is 0-based)
      return to_bcd(tm->tm_mon + 1);
    case 0x06:  // RTCYEAR: BCD year mod 100
      return to_bcd(tm->tm_year % 100);
    default:
      return 0;
  }
}

void MCP7940N::write_register(uint8_t reg, uint8_t value) {
  VLOG(2) << absl::StreamFormat("MCP7940N: write reg 0x%02X = 0x%02X", reg,
                                value);
  if (reg < sizeof(registers_)) {
    registers_[reg] = value;
  }
}

}  // namespace eight_bit
