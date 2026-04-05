// Headless emulator harness: runs the full system without a display window.
// Serial I/O is wired to the PTY; input can be fed from --input or a file,
// output is written to stdout or --output_file.
//
// Example: launch the clock program and print the time once:
//   ./headless_emulator --rom_file=../asm/rom.bin
//       --input="\r\rrun clock\rt" --input_delay_ms=200 --duration_ms=5000

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "SDL3/SDL.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "hd6301_thing.h"

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM binary");
ABSL_FLAG(std::string, sd_image_file, "", "Optional SD card image path");
ABSL_FLAG(std::string, input, "",
          "Serial input to feed (\\r = CR, \\n = LF, \\t = TAB)");
ABSL_FLAG(std::string, input_file, "",
          "File whose raw bytes are sent as serial input (appended after "
          "--input)");
ABSL_FLAG(int, input_delay_ms, 100,
          "Delay between consecutive input bytes (ms)");
ABSL_FLAG(int, duration_ms, 5000, "How long to run before exiting (ms)");
ABSL_FLAG(std::string, output_file, "-",
          "File to write serial output (- = stdout)");

namespace {
std::string unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[++i]) {
        case 'r':  out += '\r'; break;
        case 'n':  out += '\n'; break;
        case 't':  out += '\t'; break;
        case '\\': out += '\\'; break;
        default:   out += '\\'; out += s[i]; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}
}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Use dummy/offscreen SDL drivers so no display or audio device is needed.
  setenv("SDL_VIDEODRIVER", "offscreen", /*overwrite=*/1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    // Non-fatal: software surfaces (used by Graphics) work even if video
    // init fails on some platforms.
    LOG(WARNING) << "SDL_Init failed: " << SDL_GetError();
  }
  // SDL_Quit must run AFTER all SDL objects (SoundOPL3 audio stream) are
  // destroyed, so declare the cleanup before hd6301_thing_or.
  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  auto hd6301_thing_or = eight_bit::HD6301Thing::create(
      1000000, eight_bit::HD6301Thing::kKeyboard65C22);
  QCHECK_OK(hd6301_thing_or);
  auto& sys = *hd6301_thing_or;

  // Load ROM.
  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "--rom_file is required";
  {
    std::ifstream f(absl::GetFlag(FLAGS_rom_file), std::ios::binary);
    QCHECK(f.is_open()) << "Cannot open ROM: " << absl::GetFlag(FLAGS_rom_file);
    std::vector<uint8_t> rom(std::istreambuf_iterator<char>(f), {});
    constexpr uint16_t kRomSize = 0x8000;
    sys->load_rom(kRomSize - static_cast<uint16_t>(rom.size()), rom);
  }

  // Optional SD card image.
  if (!absl::GetFlag(FLAGS_sd_image_file).empty()) {
    auto ss = std::make_unique<std::stringstream>();
    std::ifstream f(absl::GetFlag(FLAGS_sd_image_file), std::ios::binary);
    QCHECK(f.is_open()) << "Cannot open SD image";
    *ss << f.rdbuf();
    sys->load_sd_image(std::move(ss));
  }

  // Open the PTY slave end for serial I/O.
  // The serial's read thread may recreate the PTY on POLLHUP (triggered when
  // no slave is connected), so retry until the name stabilises.
  int pty_fd = -1;
  for (int attempt = 0; attempt < 20 && pty_fd < 0; ++attempt) {
    const std::string pty1 = sys->get_serial_pty_name();
    pty_fd = ::open(pty1.c_str(), O_RDWR | O_NOCTTY);
    if (pty_fd < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    // Verify the name didn't change while we were opening.
    const std::string pty2 = sys->get_serial_pty_name();
    if (pty1 != pty2) {
      ::close(pty_fd);
      pty_fd = -1;
    } else {
      std::cout << "Serial PTY: " << pty1 << "\n";
    }
  }
  QCHECK(pty_fd >= 0) << "Could not open serial PTY after retries";
  {
    struct termios tty;
    if (tcgetattr(pty_fd, &tty) == 0) {
      cfmakeraw(&tty);
      tcsetattr(pty_fd, TCSANOW, &tty);
    }
  }
  fcntl(pty_fd, F_SETFL, O_NONBLOCK);

  // Build the input byte sequence.
  std::string input_seq = unescape(absl::GetFlag(FLAGS_input));
  if (!absl::GetFlag(FLAGS_input_file).empty()) {
    std::ifstream f(absl::GetFlag(FLAGS_input_file), std::ios::binary);
    QCHECK(f.is_open()) << "Cannot open input file";
    input_seq += std::string(std::istreambuf_iterator<char>(f), {});
  }

  // Open the output stream.
  std::ofstream out_file;
  std::ostream* out = &std::cout;
  if (absl::GetFlag(FLAGS_output_file) != "-") {
    out_file.open(absl::GetFlag(FLAGS_output_file));
    QCHECK(out_file.is_open()) << "Cannot open output file";
    out = &out_file;
  }

  // Start the emulation loop in its background thread.
  sys->run();

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(absl::GetFlag(FLAGS_duration_ms));
  const int delay_ms = absl::GetFlag(FLAGS_input_delay_ms);

  // Feed input bytes one at a time with the configured delay.
  std::atomic<bool> stop_input{false};
  std::thread input_thread([&]() {
    for (char c : input_seq) {
      if (stop_input.load()) break;
      ::write(pty_fd, &c, 1);
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
  });

  // Poll the screen text and write the final snapshot at the deadline.
  // Also drain any bytes the SCI does send (e.g. if __enable_serial_io=1).
  std::string last_screen;
  while (std::chrono::steady_clock::now() < deadline) {
    // Drain serial output (present when __enable_serial_io=1 in the ROM).
    {
      struct pollfd pfd{pty_fd, POLLIN, 0};
      while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char buf[256];
        ssize_t n = ::read(pty_fd, buf, sizeof(buf));
        if (n > 0) out->write(buf, n);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  out->flush();

  // Write the final screen snapshot to --output_file (or stdout if "-").
  const std::string screen = sys->get_screen_text();
  if (screen != last_screen) {
    *out << "\n--- screen snapshot ---\n" << screen << "--- end ---\n";
    out->flush();
  }

  stop_input = true;
  sys->stop();
  ::close(pty_fd);
  input_thread.join();
  return 0;
  // sdl_cleanup runs here (after hd6301_thing_or is destroyed).
}
