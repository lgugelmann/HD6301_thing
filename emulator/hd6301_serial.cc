#include "hd6301_serial.h"

#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>

#include <cstdint>

#include "address_space.h"

namespace eight_bit {
namespace {
constexpr uint8_t kTransmitEnable = 0b00000010;
constexpr uint8_t kTransmitInterruptEnable = 0b00000100;
constexpr uint8_t kReceiveEnable = 0b00001000;
constexpr uint8_t kReceiveInterruptEnable = 0b00010000;
constexpr uint8_t kTransmitDataRegisterEmpty = 0b00100000;
constexpr uint8_t kReceiveDataRegisterFull = 0b10000000;

uint16_t ticks_per_bit(uint8_t rmcr) {
  switch (rmcr & 0x03) {
    case 0:
      return 16;
    case 1:
      return 128;
    case 2:
      return 1024;
    case 3:
      return 4096;
    default:
      return 0;
  }
  return 0;
}
}  // namespace

HD6301Serial::~HD6301Serial() {
  if (our_fd_ != 0) {
    close(our_fd_);
  }
  if (their_fd_ != 0) {
    close(their_fd_);
  }
}

absl::StatusOr<std::unique_ptr<HD6301Serial>> HD6301Serial::Create(
    AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt) {
  std::unique_ptr<HD6301Serial> hd6301_serial(
      new HD6301Serial(address_space, base_address, interrupt));
  auto status = hd6301_serial->initialize();
  if (!status.ok()) {
    return status;
  }
  return hd6301_serial;
}

void HD6301Serial::tick() {
  uint8_t data;
  if (::read(our_fd_, &data, 1) > 0) {
    rx_fifo_.push(data);
  }
  if (transmit_register_empty_countdown_ > 0) {
    transmit_register_empty_countdown_ -= 1;
    if (transmit_register_empty_countdown_ == 0) {
      trcsr_ |= kTransmitDataRegisterEmpty;
      if (trcsr_ & kTransmitInterruptEnable) {
        transmit_interrupt_id_ = interrupt_->set_interrupt();
      }
    }
  }
  if (receive_register_full_countdown_ > 0) {
    receive_register_full_countdown_ -= 1;
  }
  if (!rx_fifo_.empty() && receive_register_full_countdown_ == 0) {
    receive_data_register_ = rx_fifo_.front();
    rx_fifo_.pop();
    trcsr_ |= kReceiveDataRegisterFull;
    if (trcsr_ & kReceiveInterruptEnable && receive_interrupt_id_ == 0) {
      receive_interrupt_id_ = interrupt_->set_interrupt();
    }
    receive_register_full_countdown_ = ticks_per_bit(rmcr_) * 10;
  }
}

void HD6301Serial::write(uint16_t address, uint8_t data) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case 0:
      rmcr_ = data & 0x0f;
      break;
    case 1: {
      bool receive_enabled = trcsr_ & kReceiveEnable;
      trcsr_ = (trcsr_ & 0b11100000) | (data & 0b00011111);
      // Clear the queue if we just enabled receiving
      if (!receive_enabled && trcsr_ & kReceiveEnable) {
        std::queue<uint8_t> empty;
        std::swap(rx_fifo_, empty);
      }
    } break;
    case 2:
      // no writes to the receive data register
      break;
    case 3: {
      // Writing to the transmit data register clears the transmit interrupt.
      // Technically speaking one needs to also read the status register first
      // - but we ignore this here.
      if (trcsr_ & kTransmitInterruptEnable) {
        if (transmit_interrupt_id_ != 0) {
          interrupt_->clear_interrupt(transmit_interrupt_id_);
          transmit_interrupt_id_ = 0;
        }
      }
      if ((trcsr_ & kTransmitEnable) && (trcsr_ & kTransmitDataRegisterEmpty)) {
        // Clear out the transmit data register empty bit
        trcsr_ &= ~kTransmitDataRegisterEmpty;
        ::write(our_fd_, &data, 1);
        // Sending 10 bits: Start bit, 8 data bits, stop bit
        transmit_register_empty_countdown_ = ticks_per_bit(rmcr_) * 10;
      }
      break;
    }
    default:
      LOG(ERROR) << "Write to invalid HD6301Serial address: " << offset;
  }
}

uint8_t HD6301Serial::read(uint16_t address) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case 0:
      // rmcr_ is not readable
      return 0;
    case 1:
      return trcsr_;
    case 2: {
      if (receive_interrupt_id_ != 0) {
        interrupt_->clear_interrupt(receive_interrupt_id_);
        receive_interrupt_id_ = 0;
      }
      return receive_data_register_;
    }
    case 3:
      return 0;
    default:
      LOG(ERROR) << "Read from invalid HD6301Serial address: " << offset;
  }
  return 0;
}

HD6301Serial::HD6301Serial(AddressSpace* address_space, uint16_t base_address,
                           Interrupt* interrupt)
    : address_space_(address_space),
      base_address_(base_address),
      interrupt_(interrupt) {}

absl::Status HD6301Serial::initialize() {
  auto status = address_space_->register_write(
      base_address_, base_address_ + 3,
      [this](uint16_t address, uint8_t data) { write(address, data); });
  if (!status.ok()) {
    return status;
  }
  status = address_space_->register_read(
      base_address_, base_address_ + 3,
      [this](uint16_t address) { return read(address); });
  if (!status.ok()) {
    return status;
  }

  struct termios term = {};
  if (openpty(&our_fd_, &their_fd_, nullptr, &term, nullptr)) {
    return absl::InternalError("Failed to open PTY");
  }
  // Set the file descriptor to non-blocking mode
  int flags = fcntl(our_fd_, F_GETFL, 0);
  fcntl(our_fd_, F_SETFL, flags | O_NONBLOCK);

  // Drain the buffer. Somehow we get garbage bytes here.
  // TODO: figure out why.
  uint8_t data = 0;
  while (::read(our_fd_, &data, 1) > 0) {
  }

  // Reading 0 bytes just checks for errors on the PTY.
  if (::read(our_fd_, &data, 0) < 0) {
    return absl::InternalError(
        absl::StrCat("Error reading from PTY: ", strerror(errno)));
  }

  const char* term_name = ttyname(their_fd_);
  LOG(ERROR) << "Opened PTY for HD6301Serial: " << term_name;

  return absl::OkStatus();
}

}  // namespace eight_bit