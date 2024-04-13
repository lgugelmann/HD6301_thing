#include "sound_opl3.h"

#include <SDL.h>
#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>

#include <cstdint>
#include <memory>

#include "Nuked-OPL3/opl3.h"
#include "address_space.h"

namespace eight_bit {
namespace {
constexpr int kNumSamples = 1024;
}  // namespace

void SoundOPL3::AudioCallback(void* userdata, uint8_t* stream, int len) {
  auto* callback_data = static_cast<SoundOPL3::LockableOPL3Chip*>(userdata);
  // len is the length of the stream in bytes, but we generate 16-bit stereo
  // samples, and OPL3_GenerateStream expects the number of samples.
  absl::MutexLock lock(&callback_data->mutex);
  OPL3_GenerateStream(&callback_data->chip, reinterpret_cast<int16_t*>(stream),
                      len / 4);
}

SoundOPL3::SoundOPL3(AddressSpace* address_space, uint16_t base_address)
    : address_space_(address_space), base_address_(base_address) {
  OPL3_Reset(&opl3_chip_.chip, 44100);
  auto status = address_space_->register_write(
      base_address_, base_address_ + 3,
      [this](uint16_t address, uint8_t data) { write(address, data); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register write callback for OPL3: " << status;
  }
  status = address_space_->register_read(
      base_address_, base_address_, [](uint16_t) { return read_status(); });
  if (!status.ok()) {
    LOG(ERROR) << "Failed to register read callback for OPL3: " << status;
  }
}

SoundOPL3::~SoundOPL3() {
  // SDL reference counts the Init and Quit, repeated calls are ok.
  if (sdl_audio_initialized_) {
    SDL_CloseAudio();
  }
}

absl::StatusOr<std::unique_ptr<SoundOPL3>> SoundOPL3::create(
    AddressSpace* address_space, uint16_t base_address) {
  auto sound_opl3 =
      absl::WrapUnique(new SoundOPL3(address_space, base_address));
  auto status = sound_opl3->initialize();
  if (!status.ok()) {
    return status;
  }

  return sound_opl3;
}

void SoundOPL3::write(uint16_t address, uint8_t data) {
  static uint16_t write_address = 0;
  uint16_t opl_address = address - base_address_;
  switch (opl_address) {
    case 0:
      write_address = data;
      break;
    case 1: {
      absl::MutexLock lock(&opl3_chip_.mutex);
      OPL3_WriteRegBuffered(&opl3_chip_.chip, write_address, data);
      break;
    }
    case 2:
      write_address = 0x100 | data;
      break;
    default:
      LOG(ERROR) << absl::StreamFormat("Write to invalid OPL3 address: %x",
                                       opl_address);
  }
}

uint8_t SoundOPL3::read_status() {
  // TODO implement this
  return 0;
}

absl::Status SoundOPL3::initialize() {
  // Initialize SDL
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    return absl::InternalError("SDL_Init for AUDIO failed");
  }

  SDL_AudioSpec spec;
  SDL_memset(&spec, 0, sizeof(spec));
  spec.freq = 44100;
  spec.format = AUDIO_S16SYS;
  spec.channels = 2;
  spec.samples = kNumSamples;
  spec.callback = SoundOPL3::AudioCallback;
  spec.userdata = &opl3_chip_;

  if (SDL_OpenAudio(&spec, nullptr) < 0) {
    return absl::InternalError("SDL_OpenAudio failed");
  }

  sdl_audio_initialized_ = true;
  SDL_PauseAudio(0);

  return absl::OkStatus();
}

}  // namespace eight_bit