#include "sd_card_spi.h"

#include <fstream>
#include <istream>
#include <memory>
#include <spanstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "spi.h"

namespace eight_bit {
namespace {
//
// R1 bits
//
uint8_t kR1InIdleState = 0x01;
// uint8_t kR1EraseReset = 0x02;
uint8_t kR1IllegalCommand = 0x04;
uint8_t kR1CommandCRCError = 0x08;
// uint8_t kR1EraseSequenceError = 0x10;
// uint8_t kR1AddressError = 0x20;
uint8_t kR1ParameterError = 0x40;

//
// Data Error Token bits
//
uint8_t kDataErrorTokenError = 0x01;
// uint8_t kDataErrorTokenCCError = 0x02;
// uint8_t kDataErrorTokenECCFailed = 0x04;
// uint8_t kDataErrorTokenOutOfRange = 0x08;

constexpr int kBlockSize = 512;
}  // namespace

absl::StatusOr<std::unique_ptr<SDCardSPI>> SDCardSPI::create(SPI* spi) {
  auto sd_card_spi = std::unique_ptr<SDCardSPI>(new SDCardSPI(spi));

  // Create an empty 4MB backing buffer and an iostream over it.
  auto empty_image = std::make_unique<backing_buffer_type>(4 * 1024 * 1024, 0);
  auto stream =
      std::make_unique<std::basic_spanstream<char>>(std::span{*empty_image});

  auto status =
      sd_card_spi->initialize(std::move(stream), std::move(empty_image));
  if (!status.ok()) {
    return status;
  }
  return sd_card_spi;
}

absl::StatusOr<std::unique_ptr<SDCardSPI>> SDCardSPI::create(
    SPI* spi, std::string_view image_file_name, ImageMode mode) {
  auto file_stream = std::make_unique<std::fstream>();
  file_stream->open(std::string(image_file_name),
                    mode == ImageMode::kPersistedWrites
                        ? std::ios::in | std::ios::out | std::ios::binary
                        : std::ios::in | std::ios::binary);

  if (!file_stream->is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Failed to open file: ", std::string(image_file_name)));
  }

  std::unique_ptr<stream_type> stream;
  std::unique_ptr<backing_buffer_type> backing_buffer;
  switch (mode) {
    case ImageMode::kEphemeralWrites: {
      // Ephemeral writes means we need to load the entire file into memory.
      backing_buffer = std::make_unique<backing_buffer_type>(
          std::istreambuf_iterator<char>(*file_stream),
          std::istreambuf_iterator<char>());
      stream = std::make_unique<std::basic_spanstream<char>>(
          std::span{*backing_buffer});
      break;
    }
    case ImageMode::kPersistedWrites: {
      // Persisted writes means we can stream the file directly.
      stream = std::move(file_stream);
      break;
    }
  }

  auto sd_card_spi = std::unique_ptr<SDCardSPI>(new SDCardSPI(spi));
  auto status =
      sd_card_spi->initialize(std::move(stream), std::move(backing_buffer));
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

absl::Status SDCardSPI::initialize(
    std::unique_ptr<stream_type> card_image,
    std::unique_ptr<backing_buffer_type> backing_buffer) {
  backing_buffer_ = std::move(backing_buffer);
  card_image_ = std::move(card_image);
  spi_->set_byte_received_callback(
      [this](uint8_t data) { return handle_byte_in(data); });
  spi_->set_chip_select_callback(
      [this](bool enabled) { return handle_enable(enabled); });

  // Compute the number of blocks in the file or image.
  card_image_->seekg(0);
  card_image_->ignore(std::numeric_limits<std::streamsize>::max());
  block_count_ = card_image_->gcount() / kBlockSize;
  // Clear the EOF flag.
  card_image_->clear();
  card_image_->seekg(0);
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
        input_buffer_.clear();
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
          if (next_card_state_ != CardState::kUndefined) {
            card_state_ = next_card_state_;
            next_card_state_ = CardState::kUndefined;
          } else {
            card_state_ = (enabled_ ? CardState::kReady : CardState::kIdle);
          }
        }
        VLOG(1) << "SD response byte: " << absl::Hex(response, absl::kZeroPad2);
        return response;
      }
      return 0xff;
    case CardState::kDataToken: {
      if (data == 0xfe) {
        input_buffer_.clear();
        card_state_ = CardState::kData;
      }
      return 0xff;
    }
    case CardState::kData: {
      input_buffer_.push_back(data);
      // We need to read 512 bytes of data plus two CRC bytes.
      if (input_buffer_.size() == kBlockSize + 2) {
        card_image_->seekp(write_address_ * kBlockSize);
        card_image_->write(reinterpret_cast<const char*>(input_buffer_.data()),
                           kBlockSize);
        input_buffer_.clear();
        response_queue_.push(0b0000'0101);  // Data accepted
        response_queue_.push(0x00);  // Simulate busy for the duration of a byte
        card_state_ = CardState::kResponse;
      }
      return 0xff;
    }
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
      case CardState::kUndefined:
      case CardState::kCommand:
      case CardState::kResponse:
      case CardState::kDataToken:
      case CardState::kData:
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
      case 17: {  // READ_SINGLE_BLOCK
        // Set up R1
        int32_t address = command.argument;
        if (!ready_) {
          response_queue_.push(kR1IllegalCommand | kR1InIdleState);
          break;
        } else if (address >= block_count_) {
          response_queue_.push(kR1ParameterError);
          break;
        } else {
          response_queue_.push(0);
        }
        // Try to read the data
        card_image_->seekg(address * kBlockSize);
        std::array<char, kBlockSize> block;
        card_image_->read(block.data(), kBlockSize);
        if (card_image_->gcount() != kBlockSize) {
          response_queue_.push(kDataErrorTokenError);
          // Clear any EOF we might have hit.
          card_image_->clear();
          break;
        }
        // Clear any EOF we might have hit.
        card_image_->clear();
        // Add a data token
        response_queue_.push(0xfe);
        // Then the 512 byte block
        for (char byte : block) {
          response_queue_.push(byte);
        }
        // And two CRC bytes (ignored)
        response_queue_.push(0);
        response_queue_.push(0);
        break;
      }
      case 24: {  // WRITE_BLOCK
        // Set up R1
        int32_t address = command.argument;
        if (!ready_) {
          response_queue_.push(kR1IllegalCommand | kR1InIdleState);
          break;
        } else if (address >= block_count_) {
          response_queue_.push(kR1ParameterError);
          break;
        } else {
          response_queue_.push(0);
        }
        next_card_state_ = CardState::kDataToken;
        write_address_ = address;
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
