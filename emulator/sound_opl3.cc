#include "sound_opl3.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>

#include "Nuked-OPL3/opl3.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "address_space.h"

namespace eight_bit {
namespace {
// The maximum number of samples we want to generate in one go to avoid high
// audio latency.
constexpr int kMaxNumSamples = 1024;
constexpr int kBytesPerSample = 4;  // 2 channels * 16 bits per channel
}  // namespace

void SoundOPL3::AudioCallback(void* userdata, SDL_AudioStream* stream,
                              int additional_amount, int total_amount) {
  auto* callback_data = static_cast<SoundOPL3::LockableOPL3Chip*>(userdata);
  // additional_amount is the minimum required right now, total_amount is the
  // max we can send. Both are in bytes. OPL3_GenerateStream tracks length in
  // samples however. We generate at least the minimum amount of samples to
  // avoid underruns, without going over KMaxNumSamples if possible.
  int sample_count =
      std::max(additional_amount / kBytesPerSample,
               std::min(total_amount / kBytesPerSample, kMaxNumSamples));
  static char buffer[kMaxNumSamples * kBytesPerSample];
  absl::MutexLock lock(&callback_data->mutex);
  OPL3_GenerateStream(&callback_data->chip, reinterpret_cast<int16_t*>(buffer),
                      sample_count);
  SDL_PutAudioStreamData(stream, buffer, sample_count * kBytesPerSample);
}

SoundOPL3::SoundOPL3(AddressSpace* address_space, uint16_t base_address)
    : address_space_(address_space), base_address_(base_address) {
  absl::MutexLock lock(&opl3_chip_.mutex);
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
  if (sdl_audio_stream_) {
    // This also closes the audio device as we're opening the stream with
    // SDL_OpenAudioDeviceStream.
    SDL_DestroyAudioStream(sdl_audio_stream_);
    sdl_audio_stream_ = nullptr;
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
  if (!SDL_Init(SDL_INIT_AUDIO)) {
    return absl::InternalError("SDL_Init for AUDIO failed");
  }

  SDL_AudioSpec spec;
  SDL_memset(&spec, 0, sizeof(spec));
  spec.freq = 44100;
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;

  sdl_audio_stream_ =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                &SoundOPL3::AudioCallback, &opl3_chip_);
  if (sdl_audio_stream_ == nullptr) {
    return absl::InternalError("SDL_OpenAudio failed");
  }

  SDL_ResumeAudioStreamDevice(sdl_audio_stream_);

  return absl::OkStatus();
}

}  // namespace eight_bit