#ifndef EIGHT_BIT_SD_CARD_SPI_H
#define EIGHT_BIT_SD_CARD_SPI_H

#include <array>
#include <memory>
#include <queue>

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
  static absl::StatusOr<std::unique_ptr<SDCardSPI>> create(SPI* spi);

 private:
  struct Command {
    static absl::StatusOr<Command> create(
        const std::vector<uint8_t>& input_buffer);

    uint8_t command = 0xff;
    uint32_t argument = 0;
    uint8_t crc = 0;
  };

  enum class CardState { kIdle, kReady, kCommand, kResponse };

  SDCardSPI(SPI* spi);
  absl::Status initialize();
  uint8_t handle_byte_in(uint8_t data);
  uint8_t handle_enable(bool enabled);

  void handle_command(const Command& command);

  SPI* spi_;
  bool enabled_ = false;

  // Whether or not we're considered initialized.
  bool ready_ = false;
  // Whether or not the next command is an app command.
  bool next_is_app_command_ = false;
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