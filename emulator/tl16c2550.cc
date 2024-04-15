#include "tl16c2550.h"

#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <queue>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {
namespace {
// Interrupt Enable Register constants
constexpr uint8_t kIerEnableReceivedInterrupt = 0b00000001;
// Interrupt identification register constants
constexpr uint8_t kIirReceiveInterrupt = 0x04;
// Line status register constants
constexpr uint8_t kLsrDataReady = 0b00000001;
}  // namespace

TL16C2550::~TL16C2550() {
  ::write(shutdown_fd_[1], "x", 1);
  if (read_thread_.joinable()) {
    read_thread_.join();
  }
  if (our_fd_ != 0) {
    close(our_fd_);
  }
  if (their_fd_ != 0) {
    close(their_fd_);
  }
  if (shutdown_fd_[0] != 0) {
    close(shutdown_fd_[0]);
  }
  if (shutdown_fd_[1] != 0) {
    close(shutdown_fd_[1]);
  }
}

absl::StatusOr<std::unique_ptr<TL16C2550>> TL16C2550::create(
    AddressSpace* address_space, uint16_t base_address, Interrupt* interrupt) {
  std::unique_ptr<TL16C2550> tl16c2550(
      new TL16C2550(address_space, base_address, interrupt));
  auto status = tl16c2550->initialize();
  if (!status.ok()) {
    return status;
  }
  return tl16c2550;
}

uint8_t TL16C2550::read(uint16_t address) {
  uint16_t offset = address - base_address_;
  switch (offset) {
    case 0: {
      absl::MutexLock lock(&io_mutex_);
      if (receive_data_available_irq_id_ != 0) {
        interrupt_->clear_interrupt(receive_data_available_irq_id_);
        receive_data_available_irq_id_ = 0;
      }
      if (rx_fifo_.empty()) {
        // TODO: check if we should return the last received byte instead of 0
        return 0;
      }
      uint8_t data = rx_fifo_.front();
      rx_fifo_.pop();
      if (rx_fifo_.empty()) {
        line_status_register_ &= ~kLsrDataReady;
      }
      return data;
    }
    case 1:
      return interrupt_ident_register_;
    case 2:
      return fifo_control_register_;
    case 3:
      return line_control_register_;
    case 4:
      return modem_control_register_;
    case 5:
      return line_status_register_;
    case 6:
      return modem_status_register_;
    case 7:
      return scratch_register_;
    default:
      LOG(ERROR) << "Read from invalid TL16C2550 address: "
                 << absl::Hex(offset, absl::kZeroPad2);
      return 0;
  }
}

void TL16C2550::write(uint16_t address, uint8_t data) {
  uint16_t offset = address - base_address_;
  // TODO: this only implements one of the two UARTs in the TL16C2550
  switch (offset) {
    case 0:
      ::write(our_fd_, &data, 1);
      break;
    case 1: {
      // interrupt enable register
      // TODO - implement remaining bits
      interrupt_enable_register_ = data;
      if ((interrupt_enable_register_ & 0b00000001) == 0) {
        // receive interrupt was disabled, clear any outstanding ones
        if (receive_data_available_irq_id_ != 0) {
          interrupt_->clear_interrupt(receive_data_available_irq_id_);
          receive_data_available_irq_id_ = 0;
        }
      }
      break;
    }
    case 2:
      // fifo control register
      LOG(ERROR) << "FIFO control register not implemented";
      break;
    case 3:
      // line control register
      LOG(ERROR) << "Line control register not implemented";
      break;
    case 4:
      // modem control register
      LOG(ERROR) << "Modem control register not implemented";
      break;
    case 5:
      // line status register
      LOG(ERROR) << "Line status register not implemented";
      break;
    case 6:
      // modem status register
      LOG(ERROR) << "Modem status register not implemented";
      break;
    case 7:
      // scratch register
      scratch_register_ = data;
      break;
    default:
      LOG(ERROR) << "Write to invalid TL16C2550 address: "
                 << absl::Hex(offset, absl::kZeroPad2);
      break;
  }
}

std::string TL16C2550::get_pty_name(int uart_number) const {
  if (uart_number != 0) {
    return "";
  }
  return ttyname(their_fd_);
}

TL16C2550::TL16C2550(AddressSpace* address_space, uint16_t base_address,
                     Interrupt* interrupt)
    : address_space_(address_space),
      base_address_(base_address),
      interrupt_(interrupt) {}

absl::Status TL16C2550::initialize() {
  auto status = address_space_->register_write(
      base_address_, base_address_ + 15,
      [this](uint16_t address, uint8_t data) { write(address, data); });
  if (!status.ok()) {
    return status;
  }
  status = address_space_->register_read(
      base_address_, base_address_ + 15,
      [this](uint16_t address) { return read(address); });
  if (!status.ok()) {
    return status;
  }

  struct termios term;
  if (openpty(&our_fd_, &their_fd_, nullptr, &term, nullptr)) {
    return absl::InternalError("Failed to open PTY");
  }

  // Create a pipe to signal the read thread to stop.
  if (pipe(shutdown_fd_.data()) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to create pipe: ", strerror(errno)));
  }
  read_thread_ = std::thread([this]() {
    while (true) {
      std::array<struct pollfd, 2> fds;
      fds[0].fd = our_fd_;
      fds[0].events = POLLIN;
      // The read end of the pipe for the shutdown signal.
      fds[1].fd = shutdown_fd_[0];
      fds[1].events = POLLIN;

      int retval = poll(fds.data(), fds.size(), -1);  // -1 means no timeout
      if (retval == -1) {
        perror("poll");
        continue;
      }
      if (retval > 0) {
        if (fds[0].revents & POLLIN) {
          uint8_t data;
          if (::read(our_fd_, &data, 1) > 0) {
            absl::MutexLock lock(&io_mutex_);
            rx_fifo_.push(data);
            line_status_register_ |= kLsrDataReady;
            if ((interrupt_enable_register_ & kIerEnableReceivedInterrupt) !=
                0) {
              // There is a hierarchy of interrupts, but for now we only support
              // the read one. TODO: implement the rest.
              interrupt_ident_register_ = kIirReceiveInterrupt;
              if (receive_data_available_irq_id_ == 0) {
                receive_data_available_irq_id_ = interrupt_->set_interrupt();
              }
            }
          }
        }
        if (fds[1].revents & POLLIN) {
          // We've been woken up by a write on the shutdown pipe. Stop the
          // thread.
          break;
        }
      }
    }
  });

  return absl::OkStatus();
}

}  // namespace eight_bit