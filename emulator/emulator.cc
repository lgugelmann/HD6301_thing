#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "../disassembler/disassembler.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "address_space.h"
#include "cpu6301.h"
#include "graphics.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "ps2_keyboard.h"
#include "ram.h"
#include "rom.h"
#include "sd_card_spi.h"
#include "sound_opl3.h"
#include "spi.h"
#include "tl16c2550.h"
#include "w65c22.h"

#ifdef HAVE_MIDI
#include "midi_to_serial.h"
#endif

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM file to load");
ABSL_FLAG(std::string, sd_image_file, "",
          "Path to an image to use in SD card emulation");
ABSL_FLAG(
    bool, sd_image_persist_writes, false,
    "If true, writes to the SD card image are persisted in the image file");
ABSL_FLAG(int, ticks_per_second, 1000000, "Number of CPU ticks per second");

constexpr int kDebugWindowWidth = 400;
constexpr int kGraphicsFrameWidth = 800;
constexpr int kGraphicsFrameHeight = 600;

ABSL_CONST_INIT absl::Mutex emulator_mutex_(absl::kConstInit);
std::atomic<bool> cpu_running(true);
std::atomic<bool> emulator_running(true);

// We run the emulator in a separate thread with some signaling variables
// instead of e.g. SDL timer callbacks as it makes profiling a lot easier. With
// SDL timers we'd need debugging symbols all the way into the graphics stack as
// timers go through that in some circumstances.
void emulator_loop(eight_bit::Cpu6301* cpu) {
  static const int ticks_to_run = absl::GetFlag(FLAGS_ticks_per_second) / 1000;
  static int extra_ticks = 0;  // How many extra ticks we ran last time

  while (emulator_running) {
    static auto next_loop =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    std::this_thread::sleep_until(next_loop);
    next_loop += std::chrono::milliseconds(1);

    // Warn if the emulator can't keep up with real time.
    if (next_loop <
        std::chrono::steady_clock::now() - std::chrono::milliseconds(100)) {
      LOG_EVERY_N_SEC(ERROR, 1)
          << "Emulator is running behind real time by "
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - next_loop)
                 .count()
          << "ms";
    }

    if (cpu_running) {
      absl::MutexLock lock(&emulator_mutex_);
      auto result = cpu->tick(ticks_to_run - extra_ticks);
      extra_ticks = result.cycles_run - ticks_to_run;
      if (result.breakpoint_hit) {
        cpu_running = false;
      }
    }
  }
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    LOG(FATAL) << absl::StreamFormat("Failed to initialize SDL: %s",
                                     SDL_GetError());
  }
  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    LOG(ERROR) << "SDL can not disable compositor bypass - not running under "
                  "Linux?";
  }

  // Scale the window 2x if the screen has high DPI
  float dpi = 0.0;
  SDL_GetDisplayDPI(0, nullptr, &dpi, nullptr);
  int scale = 1;
  if (dpi > 120.0) {
    scale = 2;
  }

  auto* window = SDL_CreateWindow(
      "Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      (kGraphicsFrameWidth + kDebugWindowWidth) * scale,
      kGraphicsFrameHeight * scale, SDL_WINDOW_SHOWN);
  if (!window) {
    LOG(FATAL) << absl::StreamFormat("Failed to create window: %s",
                                     SDL_GetError());
  }
  absl::Cleanup window_cleanup([window] { SDL_DestroyWindow(window); });

  // Create a renderer
  auto* renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    LOG(FATAL) << absl::StreamFormat("Failed to create renderer: %s",
                                     SDL_GetError());
  }
  absl::Cleanup renderer_cleanup([renderer] { SDL_DestroyRenderer(renderer); });

  // Init to a black background
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  absl::Cleanup imgui_cleanup([] { ImGui::DestroyContext(); });

  ImGuiIO& io = ImGui::GetIO();

  // Disable creation of imgui.ini files
  io.IniFilename = nullptr;

  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(scale);

  io.Fonts->ClearFonts();
  auto font_config = ImFontConfig();
  font_config.SizePixels = 13.0F * scale;
  io.Fonts->AddFontDefault(&font_config);

  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  absl::Cleanup imgui_sdl_cleanup([] { ImGui_ImplSDL2_Shutdown(); });
  ImGui_ImplSDLRenderer2_Init(renderer);
  absl::Cleanup imgui_renderer_cleanup(
      [] { ImGui_ImplSDLRenderer2_Shutdown(); });

  eight_bit::AddressSpace address_space;

  constexpr uint rom_start = 0x8000;
  constexpr uint rom_size = 0x8000;
  auto rom_or = eight_bit::Rom::create(&address_space, rom_start, rom_size);
  QCHECK_OK(rom_or);
  auto rom = std::move(rom_or.value());

  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "No ROM file specified.";
  const std::string rom_file_name = absl::GetFlag(FLAGS_rom_file);
  std::ifstream rom_file(rom_file_name, std::ios::binary);
  QCHECK(rom_file.is_open()) << "Failed to open file: " << rom_file_name;

  std::vector<uint8_t> rom_data(std::istreambuf_iterator<char>(rom_file), {});
  uint16_t rom_load_address = rom_size - rom_data.size();
  rom->load(rom_load_address, rom_data);

  eight_bit::Disassembler disassembler;
  QCHECK_OK(disassembler.set_data(rom_start + rom_load_address, rom_data));
  QCHECK_OK(disassembler.disassemble());

  // 0..1f is internal CPU registers and otherwise reserved
  auto ram_or = eight_bit::Ram::create(&address_space, 0x0020, 0x7f00 - 0x0020);
  QCHECK_OK(ram_or);

  auto graphics_or = eight_bit::Graphics::create(0x7fc0, &address_space);
  QCHECK_OK(graphics_or);
  auto graphics = std::move(graphics_or.value());

  auto cpu_or = eight_bit::Cpu6301::create(&address_space);
  QCHECK_OK(cpu_or);
  auto cpu = std::move(cpu_or.value());
  cpu->reset();
  std::cout << "CPU serial port: " << cpu->get_serial()->get_pty_name() << "\n";

  eight_bit::PS2Keyboard keyboard(cpu->get_irq(), cpu->get_port1(),
                                  cpu->get_port2());

  auto sound_opl3 = eight_bit::SoundOPL3::create(&address_space, 0x7f80);
  QCHECK_OK(sound_opl3);

  auto tl16c2550 =
      eight_bit::TL16C2550::create(&address_space, 0x7f40, cpu->get_irq());
  QCHECK_OK(tl16c2550);

  auto wd65c22 =
      eight_bit::W65C22::Create(&address_space, 0x7f20, cpu->get_irq());
  QCHECK_OK(wd65c22);
  cpu->register_tick_callback([&wd65c22]() { wd65c22.value()->tick(); });

  auto spi = eight_bit::SPI::create((*wd65c22)->port_a(), 2, 0, 1, 7);
  QCHECK_OK(spi);

  std::string sd_image_file = absl::GetFlag(FLAGS_sd_image_file);
  absl::StatusOr<std::unique_ptr<eight_bit::SDCardSPI>> sd_card_spi;
  if (sd_image_file.empty()) {
    sd_card_spi = eight_bit::SDCardSPI::create((*spi).get());
  } else {
    eight_bit::SDCardSPI::ImageMode mode =
        absl::GetFlag(FLAGS_sd_image_persist_writes)
            ? eight_bit::SDCardSPI::ImageMode::kPersistedWrites
            : eight_bit::SDCardSPI::ImageMode::kEphemeralWrites;
    sd_card_spi =
        eight_bit::SDCardSPI::create((*spi).get(), sd_image_file, mode);
  }
  QCHECK_OK(sd_card_spi);

#ifdef HAVE_MIDI
  auto midi_to_serial =
      eight_bit::MidiToSerial::create((*tl16c2550)->get_pty_name(0));
  QCHECK_OK(midi_to_serial);
#endif

  std::thread emulator_thread(emulator_loop, cpu.get());

  // Create an event handler
  SDL_Event event;

  // Main loop
  while (emulator_running) {
    // Handle events
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      if (event.type == SDL_QUIT) {
        emulator_running = false;
      }
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        // Only pass keyboard events if Dear ImGui doesn't want them
        if (!io.WantCaptureKeyboard) {
          keyboard.handle_keyboard_event(event.key);
        }
      }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(kGraphicsFrameWidth * scale, 0));
    ImGui::SetNextWindowSize(
        ImVec2(kDebugWindowWidth * scale, kGraphicsFrameHeight * scale));
    ImGui::Begin("Emulator controls", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);

    static std::string button_text = "Pause";
    static eight_bit::Cpu6301::CpuState cpu_state;
    static bool ui_in_running_state = true;
    if (ImGui::Button(button_text.c_str(), ImVec2(-1, 0))) {
      if (!cpu_running) {
        // If we're paused we might be on a breakpoint and the program would
        // immediately stop again on the same instruction. Make sure we move one
        // instruction past the breakpoint.
        cpu->tick(1, true /* ignore_breakpoint */);
      }
      cpu_running = !cpu_running;
    }
    if (cpu_running != ui_in_running_state) {
      if (cpu_running) {
        button_text = "Pause";
      } else {
        cpu_running = false;
        button_text = "Run";
        absl::MutexLock lock(&emulator_mutex_);
        cpu_state = cpu->get_state();
      }
      ui_in_running_state = cpu_running;
    }
    ImGui::BeginDisabled(cpu_running);
    if (ImGui::Button("Step", ImVec2(-1, 0))) {
      absl::MutexLock lock(&emulator_mutex_);
      cpu->tick(1, true /* ignore_breakpoint */);
      cpu_state = cpu->get_state();
    }

    // Breakpoint handling
    ImGui::SeparatorText("Breakpoints");
    static std::string breakpoint_str("0000");
    ImGui::InputText("Address", breakpoint_str.data(),
                     breakpoint_str.size() + 1,
                     ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::Button("Set breakpoint", ImVec2(-1, 0))) {
      // SimleAtoi only works with 32 or 64 bit ints.
      int breakpoint;
      if (absl::SimpleHexAtoi(breakpoint_str, &breakpoint) && breakpoint >= 0 &&
          breakpoint <= 0xffff) {
        absl::MutexLock lock(&emulator_mutex_);
        cpu->set_breakpoint((uint16_t)breakpoint);
        cpu_state = cpu->get_state();
      } else {
        // Bare-bones error handling as the ImGuiInputTextFlags_CharsHexadecimal
        // above should prevent an invalid address from being settable.
        LOG(ERROR) << "Invalid breakpoint address: " << breakpoint_str;
      }
    }
    if (cpu_state.breakpoint.has_value()) {
      ImGui::Text("Breakpoint set at 0x%04X", cpu_state.breakpoint.value());
      ImGui::SameLine();
      if (ImGui::Button("Clear breakpoint")) {
        absl::MutexLock lock(&emulator_mutex_);
        cpu->clear_breakpoint();
        cpu_state = cpu->get_state();
      }
    } else {
      ImGui::Text("No breakpoint set");
    }

    // CPU state
    ImGui::SeparatorText("CPU State");
    ImGui::Text(" A: 0x%02X        ", cpu_state.a);
    ImGui::SameLine();
    ImGui::Text(" X: 0x%04X", cpu_state.x);
    ImGui::Text(" B: 0x%02X        ", cpu_state.b);
    ImGui::SameLine();
    ImGui::Text("PC: 0x%04X", cpu_state.pc);
    int sr = cpu_state.sr;
    ImGui::Text("SR: HINZVC      \n    %d%d%d%d%d%d\n", (sr & 0x20) >> 5,
                (sr & 0x10) >> 4, (sr & 0x08) >> 3, (sr & 0x04) >> 2,
                (sr & 0x02) >> 1, sr & 0x01);
    ImGui::SameLine();
    ImGui::Text("SP: 0x%04X", cpu_state.sp);

    // Disassembly
    ImGui::SeparatorText("Disassembly");
    static std::string pre_context;
    static std::string disassembly;
    static std::string post_context;
    if (!cpu_running) {
      static constexpr int kPreContext = 4;
      static constexpr int kPostContext = 7;

      static uint16_t last_pc = 0;
      if (last_pc != cpu_state.pc) {
        last_pc = cpu_state.pc;
        disassembler.set_instruction_boundary_hint(cpu_state.pc);
        QCHECK_OK(disassembler.disassemble());
        const auto& disassembly_vector = disassembler.disassembly();
        pre_context.clear();
        // Find kPreContext many non-empty lines before the current PC
        for (size_t i = 1, found = 0; found < kPreContext; ++i) {
          if (i > last_pc) {
            break;
          }
          if (!disassembly_vector[last_pc - i].empty()) {
            // Not the most efficient way to do this, but good enough.
            pre_context =
                absl::StrCat(disassembly_vector[last_pc - i], pre_context);
            ++found;
          }
        }
        disassembly = disassembly_vector[last_pc];
        post_context.clear();
        // Find kPostContext many non-empty lines after the current PC
        for (size_t i = 1, found = 0; found < kPostContext; ++i) {
          if (last_pc + i >= disassembly_vector.size()) {
            break;
          }
          if (!disassembly_vector[last_pc + i].empty()) {
            absl::StrAppend(&post_context, disassembly_vector[last_pc + i]);
            ++found;
          }
        }
      }
    }
    ImGui::Text("%s", pre_context.c_str());
    ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "%s",
                       disassembly.c_str());
    ImGui::Text("%s", post_context.c_str());

    ImGui::SetCursorPosY(ImGui::GetWindowSize().y -
                         ImGui::GetFrameHeightWithSpacing() * 2 -
                         ImGui::GetStyle().ItemSpacing.y);
    static bool show_ram_hexdump = false;
    if (ImGui::Button("RAM Hexdump", ImVec2(-1, 0))) {
      show_ram_hexdump = true;
      (*ram_or)->hexdump();
    }
    if (ImGui::Button("Reset", ImVec2(-1, 0))) {
      absl::MutexLock lock(&emulator_mutex_);
      cpu->reset();
      cpu_state = cpu->get_state();
    }
    if (show_ram_hexdump) {
      static std::string ram_hexdump;
      static uint16_t last_pc = 0;
      if (last_pc != cpu_state.pc) {
        last_pc = cpu_state.pc;
        ram_hexdump = (*ram_or)->hexdump();
      }
      ImGui::Begin("RAM Hexdump", &show_ram_hexdump);
      ImGui::Text("%s", ram_hexdump.c_str());
      ImGui::End();
    }

    ImGui::EndDisabled();

    ImGui::End();

    ImGui::Render();

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    //
    SDL_Rect graphics_rect = {0, 0, kGraphicsFrameWidth * scale,
                              kGraphicsFrameHeight * scale};
    auto status = graphics->render(renderer, &graphics_rect);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to render graphics: " << status;
      break;
    }
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
  }
  emulator_running = false;
  emulator_thread.join();

  return 0;
}
