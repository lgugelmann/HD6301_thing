#ifndef EIGHT_BIT_MIDI_TO_SERIAL_H
#define EIGHT_BIT_MIDI_TO_SERIAL_H

#include <rtmidi/RtMidi.h>

#include <memory>
#include <string_view>

#include "absl/status/statusor.h"

namespace eight_bit {

// A class that creates a virtual MIDI port and forwards incoming MIDI bytes to
// a PTY
class MidiToSerial {
 public:
  MidiToSerial(const MidiToSerial&) = delete;
  MidiToSerial& operator=(const MidiToSerial&) = delete;
  ~MidiToSerial();

  // pty_name is the name of the pseudo-terminal to write to.
  static absl::StatusOr<std::unique_ptr<MidiToSerial>> create(
      std::string_view pty_name);

 private:
  MidiToSerial() = default;

  absl::Status initialize(std::string_view pty_name);

  int fd_ = -1;
  std::unique_ptr<RtMidiIn> midi_in_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_MIDI_TO_SERIAL_H