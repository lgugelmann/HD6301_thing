#include "sd_card_spi.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "spi.h"

namespace eight_bit {
namespace {
uint8_t kR1InIdleState = 0x01;
// uint8_t kR1EraseReset = 0x02;
uint8_t kR1IllegalCommand = 0x04;
uint8_t kR1CommandCRCError = 0x08;
// uint8_t kR1EraseSequenceError = 0x10;
// uint8_t kR1AddressError = 0x20;
// uint8_t kR1ParameterError = 0x40;
}  // namespace

absl::StatusOr<std::unique_ptr<SDCardSPI>> SDCardSPI::create(SPI* spi) {
  auto sd_card_spi = std::unique_ptr<SDCardSPI>(new SDCardSPI(spi));
  auto status = sd_card_spi->initialize();
  if (!status.ok()) {
    return status;
  }
  return sd_card_spi;
}

absl::StatusOr<SDCardSPI::Command> SDCardSPI::Command::create(
    const std::vector<uint8_t>& input_buffer) {
  if (input_buffer.size() != 6) {
    return absl::InvalidArgumentError("Invalid command buffer size");
  }
  Command command;
  command.command = input_buffer[0];
  command.argument = (input_buffer[1] << 24) | (input_buffer[2] << 16) |
                     (input_buffer[3] << 8) | input_buffer[4];
  command.crc = input_buffer[5];
  return command;
}

SDCardSPI::SDCardSPI(SPI* spi) : spi_(spi) {}

absl::Status SDCardSPI::initialize() {
  spi_->set_byte_received_callback(
      [this](uint8_t data) { return handle_byte_in(data); });
  spi_->set_chip_select_callback(
      [this](bool enabled) { return handle_enable(enabled); });
  return absl::OkStatus();
}

uint8_t SDCardSPI::handle_byte_in(uint8_t data) {
  VLOG(1) << "SD received byte: " << absl::Hex(data, absl::kZeroPad2);
  if (!enabled_) {
    return 0xff;
  }
  switch (card_state_) {
    case CardState::kIdle:
    case CardState::kReady:
      return 0xff;
    case CardState::kCommand:
      input_buffer_.push_back(data);
      // Command byte, 4 argument bytes, and CRC byte makes 6 bytes total.
      if (input_buffer_.size() == 6) {
        auto command_or = Command::create(input_buffer_);
        if (!command_or.ok()) {
          LOG(ERROR) << "Failed to parse command: " << command_or.status();
          card_state_ = CardState::kIdle;
          return 0xff;
        }
        handle_command(command_or.value());
        card_state_ = CardState::kResponse;
        // Note how we return 0xff here instead of the first byte of the
        // response. Real cards seem to insert this extra 0xff byte too.
        return 0xff;
      }
      return 0xff;
    case CardState::kResponse:
      if (!response_queue_.empty()) {
        uint8_t response = response_queue_.front();
        response_queue_.pop();
        if (response_queue_.empty()) {
          card_state_ = (enabled_ ? CardState::kReady : CardState::kIdle);
        }
        VLOG(1) << "SD response byte: " << absl::Hex(response, absl::kZeroPad2);
        return response;
      }
      return 0xff;
    default:
      LOG(ERROR) << "Unexpected card state: " << static_cast<int>(card_state_);
      return 0;
  }
}

uint8_t SDCardSPI::handle_enable(bool enabled) {
  VLOG(1) << "SD card " << (enabled ? "enabled" : "disabled");
  enabled_ = enabled;
  if (enabled) {
    input_buffer_.clear();
    response_queue_ = std::queue<uint8_t>();
    switch (card_state_) {
      case CardState::kIdle:
      case CardState::kReady:
        card_state_ = CardState::kCommand;
        break;
      case CardState::kCommand:
      case CardState::kResponse:
        LOG(ERROR) << "Unexpected SD card state on SPI enabled: "
                   << static_cast<int>(card_state_);
        card_state_ = CardState::kCommand;
        break;
    }
  } else {
    card_state_ = ready_ ? CardState::kReady : CardState::kIdle;
  };
  // We need to pick the byte that will be shifted out while we read the first
  // input byte. We have no data to send so it doesn't matter. Real cards use
  // 0xff so we do too.
  return 0xff;
}

void SDCardSPI::handle_command(const Command& command) {
  uint8_t command_number = command.command & 0x3f;
  VLOG(1) << "SD card command: " << absl::Hex(command.command, absl::kZeroPad2)
          << " (" << (int)command_number << ") "
          << absl::Hex(command.argument, absl::kZeroPad8) << " "
          << absl::Hex(command.crc, absl::kZeroPad2);
  if (!next_is_app_command_) {
    switch (command_number) {
      case 0: {  // GO_IDLE_STATE
        ready_ = false;
        uint8_t r1 = kR1InIdleState;
        if (command.crc != 0x95) {
          r1 |= kR1CommandCRCError;
        }
        response_queue_.push(r1);
        break;
      }
      case 8: {  // SEND_IF_COND
        // TODO: implement CRC check (required here)
        uint8_t r1 = kR1InIdleState;
        response_queue_.push(r1);
        response_queue_.push(0x00);
        response_queue_.push(0x00);
        // Support 2.7-3.6V
        response_queue_.push(0x01);
        // Check pattern echo
        response_queue_.push(command.argument & 0xff);
        break;
      }
      case 55: {  // APP_CMD
        VLOG(1) << "Next SD card command is App command";
        response_queue_.push(ready_ ? 0 : kR1InIdleState);
        next_is_app_command_ = true;
        break;
      }
      case 58: {  // READ_OCR
        uint8_t r1 = 0;
        if (!ready_) {
          r1 |= kR1InIdleState;
        }
        response_queue_.push(r1);
        // OCR register. We're pretending to be an SDHC card with support for
        // all voltages between 2.7 and 3.6V and with <2TB capacity. The
        // register is passed out MSB first.

        // bit 31 indicates ready, bit 30 indicates SDHC. SDHC bit is only valid
        // after the card is initialized. Real cards return 0 before that.
        if (ready_) {
          response_queue_.push(0xc0);
        } else {
          response_queue_.push(0x00);
        }
        response_queue_.push(0xff);  // bits 16..23 are for 2.8-3.6V
        response_queue_.push(0x80);  // bits 8..14 reserved, 15 2.7-2.8V
        response_queue_.push(0x00);  // bits 0..7 reserved
        break;
      }
      default: {
        LOG(ERROR) << "Unsupported SD card command: " << (int)command_number;
        uint8_t r1 = kR1IllegalCommand | (ready_ ? 0 : kR1InIdleState);
        response_queue_.push(r1);
        break;
      }
    }
  } else {
    next_is_app_command_ = false;
    switch (command_number) {
      case 41: {  // SD_SEND_OP_COND
        ready_ = true;
        response_queue_.push(0);
        break;
      }
      default: {
        LOG(ERROR) << "Unsupported SD card App command: "
                   << (int)command_number;
        response_queue_.push(0xff);
        break;
      }
    }
  }
}

}  // namespace eight_bit
