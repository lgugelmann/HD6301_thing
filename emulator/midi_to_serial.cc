#include "midi_to_serial.h"

#include <fcntl.h>
#include <rtmidi/RtMidi.h>
#include <unistd.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace eight_bit {

MidiToSerial::~MidiToSerial() {
  if (fd_ != -1) {
    close(fd_);
  }
}

absl::StatusOr<std::unique_ptr<MidiToSerial>> eight_bit::MidiToSerial::create(
    std::string_view pty_name) {
  std::unique_ptr<MidiToSerial> midi_to_serial(new MidiToSerial());
  auto status = midi_to_serial->initialize(pty_name);
  if (!status.ok()) {
    return status;
  }
  return midi_to_serial;
}

absl::Status eight_bit::MidiToSerial::initialize(std::string_view pty_name) {
  fd_ = open(pty_name.data(), O_WRONLY | O_NOCTTY);
  if (fd_ == -1) {
    return absl::InternalError(
        absl::StrCat("Failed to open pty ", pty_name, " for writing"));
  }

  try {
    midi_in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Emulator MIDI");
    midi_in_->openVirtualPort("Emulator MIDI in");
    midi_in_->setCallback(
        [](double /* timestamp */, std::vector<unsigned char>* message,
           void* user_data) {
          int fd = *static_cast<int*>(user_data);
          ::write(fd, message->data(), message->size());
        },
        &fd_);
  } catch (RtMidiError& error) {
    return absl::InternalError(
        absl::StrCat("Failed to open virtual MIDI port: ", error.what()));
  }
  return absl::OkStatus();
}

}  // namespace eight_bit