#ifndef EIGHT_BIT_SOUND_OPL3_H
#define EIGHT_BIT_SOUND_OPL3_H

#include <absl/base/thread_annotations.h>
#include <absl/status/statusor.h>
#include <absl/synchronization/mutex.h>

#include <cstdint>
#include <memory>

#include "Nuked-OPL3/opl3.h"
#include "address_space.h"

namespace eight_bit {

class SoundOPL3 {
 public:
  SoundOPL3(const SoundOPL3&) = delete;
  SoundOPL3& operator=(const SoundOPL3&) = delete;
  ~SoundOPL3();

  static absl::StatusOr<std::unique_ptr<SoundOPL3>> Create(
      AddressSpace* address_space, uint16_t base_address);

  void write(uint16_t address, uint8_t data);
  uint8_t read_status();

 private:
  SoundOPL3(AddressSpace* address_space, uint16_t base_address);
  absl::Status initialize();

  static void AudioCallback(void* userdata, uint8_t* stream, int len);

  bool sdl_audio_initialized_ = false;
  AddressSpace* address_space_;
  uint16_t base_address_;

  struct LockableOPL3Chip {
    absl::Mutex mutex;
    opl3_chip chip ABSL_GUARDED_BY(mutex);
  };
  LockableOPL3Chip opl3_chip_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_SOUND_OPL3_H