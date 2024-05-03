#ifndef EIGHT_BIT_SD_CARD_SPI_H
#define EIGHT_BIT_SD_CARD_SPI_H

#include <istream>
#include <memory>
#include <queue>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "spi.h"

namespace eight_bit {

// Emulates a bare-bones SD Card SPI interface. It implements only a minimal set
// of commands to make it act as an SDHC card.
class SDCardSPI {
 public:
  ~SDCardSPI() = default;
  SDCardSPI(const SDCardSPI&) = delete;

  // Create an emulated SD Card backed by 4MB of storage that's initially all
  // zeros.
  static absl::StatusOr<std::unique_ptr<SDCardSPI>> create(SPI* spi);

  enum class ImageMode {
    // The image file is loaded from disk, but changes are in-memory only. Note:
    // requires as much RAM as the size of the image file.
    kEphemeralWrites,
    // The image file is loaded from disk, and changes are written back to it.
    kPersistedWrites,
  };
  // Create an emulated SD Card loaded from a file on disk. The file must exist.
  // If mode is kPersistedWrites, changes will be written back to the file.
  static absl::StatusOr<std::unique_ptr<SDCardSPI>> create(
      SPI* spi, std::string_view image_file_name, ImageMode mode);

 private:
  using stream_type = std::basic_iostream<char>;
  using backing_buffer_type = std::vector<char>;

  struct Command {
    static absl::StatusOr<Command> create(
        const std::vector<uint8_t>& input_buffer);

    uint8_t command = 0xff;
    uint32_t argument = 0;
    uint8_t crc = 0;
  };

  enum class CardState {
    kUndefined,
    kIdle,
    kReady,
    kCommand,
    kResponse,
    kDataToken,
    kData
  };

  SDCardSPI(SPI* spi);

  absl::Status initialize(std::unique_ptr<stream_type> card_image,
                          std::unique_ptr<backing_buffer_type> backing_buffer);
  uint8_t handle_byte_in(uint8_t data);
  uint8_t handle_enable(bool enabled);

  void handle_command(const Command& command);

  SPI* spi_;
  // Note: card_image_ is defined after backing buffer to ensure it's deleted
  // first
  std::unique_ptr<backing_buffer_type> backing_buffer_;
  std::unique_ptr<stream_type> card_image_;
  int block_count_ = 0;
  bool enabled_ = false;

  // Whether or not we're considered initialized.
  bool ready_ = false;
  // Whether or not the next command is an app command.
  bool next_is_app_command_ = false;
  // The address for the next write command.
  uint32_t write_address_ = 0;
  // The current card state.
  CardState card_state_ = CardState::kIdle;
  // The card state we transition to after flushing the response queue.
  CardState next_card_state_ = CardState::kIdle;
  // A buffer to hold incoming bytes as they build to a command.
  std::vector<uint8_t> input_buffer_;
  // The queue of bytes to send back from the card.
  std::queue<uint8_t> response_queue_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_SD_CARD_SPI_H