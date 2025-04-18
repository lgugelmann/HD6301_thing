#include "hd6301_serial.h"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cstdint>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
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

absl::StatusOr<int> get_open_pty() {
  int fd = posix_openpt(O_RDWR | O_NOCTTY);
  absl::Cleanup close_fd = [&fd] { close(fd); };

  if (fd == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to open PTY:", strerror(errno)));
  }
  if (grantpt(fd) == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to grant PTY:", strerror(errno)));
  }
  if (unlockpt(fd) == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to unlock PTY:", strerror(errno)));
  }
  struct termios term;
  if (tcgetattr(fd, &term) == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to get PTY attributes:", strerror(errno)));
  }
  term.c_lflag &= ~ECHO;
  if (tcsetattr(fd, TCSANOW, &term) == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to disable echo on PTY:", strerror(errno)));
  }
  // Set the PTY to non-blocking. We write to it regardless of whether there is
  // somebody on the other end, and we don't want to block write() when the
  // buffer is full as it would block the entire emulator.
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to set PTY to non-blocking:", strerror(errno)));
  }
  std::move(close_fd).Cancel();
  return fd;
}
}  // namespace

HD6301Serial::~HD6301Serial() {
  // Signal the read thread to stop.
  if (shutdown_fd_[1] != 0) {
    uint8_t data = 0;
    ::write(shutdown_fd_[1], &data, 1);
  }
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

absl::StatusOr<std::unique_ptr<HD6301Serial>> HD6301Serial::create(
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
  if (receive_register_full_countdown_ == 0 &&
      !rx_fifo_empty_.test(std::memory_order_relaxed)) {
    absl::MutexLock lock(&mutex_);
    if (!rx_fifo_.empty()) {
      receive_data_register_ = rx_fifo_.front();
      rx_fifo_.pop();
      trcsr_ |= kReceiveDataRegisterFull;
      if (trcsr_ & kReceiveInterruptEnable && receive_interrupt_id_ == 0) {
        receive_interrupt_id_ = interrupt_->set_interrupt();
      }
      receive_register_full_countdown_ = ticks_per_bit(rmcr_) * 10;
    }
    if (rx_fifo_.empty()) {
      rx_fifo_empty_.test_and_set();
    }
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
        absl::MutexLock lock(&mutex_);
        std::swap(rx_fifo_, empty);
        rx_fifo_empty_.test_and_set();
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
        absl::MutexLock lock(&mutex_);
        ::write(our_fd_, &data, 1);
        // Sending 10 bits: Start bit, 8 data bits, stop bit
        transmit_register_empty_countdown_ = ticks_per_bit(rmcr_) * 10;
      }
      break;
    }
    default:
      LOG(ERROR) << "Write to invalid HD6301Serial address: "
                 << absl::Hex(offset, absl::kZeroPad4);
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
      LOG(ERROR) << "Read from invalid HD6301Serial address: "
                 << absl::Hex(offset, absl::kZeroPad4);
  }
  return 0;
}

std::string HD6301Serial::get_pty_name() {
  absl::MutexLock lock(&mutex_);
  return ptsname(our_fd_);
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

  auto pty_fd = get_open_pty();
  if (!pty_fd.ok()) {
    return pty_fd.status();
  }
  our_fd_ = pty_fd.value();

  // Create a pipe to signal the read thread to stop.
  if (pipe(shutdown_fd_.data()) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to create pipe: ", strerror(errno)));
  }
  read_thread_ = std::thread([this]() {
    while (true) {
      std::array<struct pollfd, 2> fds;
      fds[0].fd = our_fd_;
      fds[0].events = POLLIN | POLLHUP;
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
            absl::MutexLock lock(&mutex_);
            rx_fifo_.push(data);
            rx_fifo_empty_.clear();
          }
        }
        if (fds[0].revents & POLLHUP) {
          // The other side of our PTY has been closed. It can't be reopened so
          // we create a new one.
          absl::MutexLock lock(&mutex_);
          close(our_fd_);
          auto pty_fd = get_open_pty();
          if (!pty_fd.ok()) {
            LOG(ERROR) << "Failed to open PTY: " << pty_fd.status()
                       << ". Disabling serial port.";
            break;
          }
          our_fd_ = pty_fd.value();
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