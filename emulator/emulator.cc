#include <SDL.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
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
#include "sound_opl3.h"
#include "tl16c2550.h"

#ifdef HAVE_MIDI
#include "midi_to_serial.h"
#endif

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM file to load");
ABSL_FLAG(int, ticks_per_second, 1000000, "Number of CPU ticks per second");

// SDL starts one timer thread that runs all timer callbacks. In theory
// SDL_Quit() should shut it down, but instead it seems to just stop running the
// callbacks while the thread itself sticks around. With the thread still active
// TSan can't see that accesses from timer_callback and the destructors from the
// main thread do not interact with each other and freks out at shoutdown time.
// This mutex is cheap and avoids the false positive.
ABSL_CONST_INIT absl::Mutex callback_mutex_(absl::kConstInit);
// Taking the mutext above is too expensive, and we don't actually need to have
// any ordering guarantees. As long as the flip is atomic we don't care when
// exactly we see it.
std::atomic_flag breakpoint_hit = ATOMIC_FLAG_INIT;

constexpr int kDebugWindowWidth = 400;
constexpr int kGraphicsFrameWidth = 800;
constexpr int kGraphicsFrameHeight = 600;

// This gets called every millisecond, which corresponds to 1000 CPU ticks.
// TODO: figure out how to do this faster
Uint32 timer_callback(Uint32 interval, void* param) {
  static const int ticks_to_run = absl::GetFlag(FLAGS_ticks_per_second) / 1000;
  static int extra_ticks = 0;  // How many extra ticks we ran last time

  absl::MutexLock lock(&callback_mutex_);
  auto* cpu = static_cast<eight_bit::Cpu6301*>(param);
  auto result = cpu->tick(ticks_to_run - extra_ticks);
  extra_ticks = result.cycles_run - ticks_to_run;
  if (result.breakpoint_hit) {
    // Stop the timer if we hit a breakpoint
    breakpoint_hit.test_and_set(std::memory_order_relaxed);
    return 0;
  }
  return interval;
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

#ifdef HAVE_MIDI
  auto midi_to_serial =
      eight_bit::MidiToSerial::create((*tl16c2550)->get_pty_name(0));
  QCHECK_OK(midi_to_serial);
#endif

  // Add a timer callback to call cpu.tick() once every millisecond
  SDL_TimerID timer = SDL_AddTimer(1, timer_callback, cpu.get());
  if (timer == 0) {
    LOG(FATAL) << absl::StreamFormat("Failed to create timer: %s",
                                     SDL_GetError());
  }

  // Create an event handler
  SDL_Event event;

  // Main loop
  bool running = true;
  while (running) {
    // Handle events
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      if (event.type == SDL_QUIT) {
        running = false;
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
    static bool cpu_running = true;
    static eight_bit::Cpu6301::CpuState cpu_state;
    bool to_stop_state = false;
    bool to_run_state = false;
    if (ImGui::Button(button_text.c_str(), ImVec2(-1, 0))) {
      if (cpu_running) {
        to_stop_state = true;
      } else {
        to_run_state = true;
      }
    }
    {
      if (breakpoint_hit.test()) {
        to_stop_state = true;
        breakpoint_hit.clear();
      }
    }
    if (to_stop_state) {
      SDL_RemoveTimer(timer);
      timer = 0;
      cpu_running = false;
      button_text = "Run";
      absl::MutexLock lock(&callback_mutex_);
      cpu_state = cpu->get_state();
    } else if (to_run_state) {
      timer = SDL_AddTimer(1, timer_callback, cpu.get());
      button_text = "Pause";
      cpu_running = true;
    }
    ImGui::BeginDisabled(cpu_running);
    if (ImGui::Button("Step", ImVec2(-1, 0))) {
      absl::MutexLock lock(&callback_mutex_);
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
        absl::MutexLock lock(&callback_mutex_);
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
        absl::MutexLock lock(&callback_mutex_);
        cpu->clear_breakpoint();
        cpu_state = cpu->get_state();
      }
    } else {
      ImGui::Text("No breakpoint set");
    }

    // CPU state
    ImGui::SeparatorText("CPU State");
    ImGui::Text(" A: 0x%02X", cpu_state.a);
    ImGui::Text(" B: 0x%02X", cpu_state.b);
    ImGui::Text(" X: 0x%04X", cpu_state.x);
    ImGui::Text("PC: 0x%04X", cpu_state.pc);
    ImGui::Text("SP: 0x%04X", cpu_state.sp);
    int sr = cpu_state.sr;
    ImGui::Text("SR: HINZVC\n    %d%d%d%d%d%d\n", (sr & 0x20) >> 5,
                (sr & 0x10) >> 4, (sr & 0x08) >> 3, (sr & 0x04) >> 2,
                (sr & 0x02) >> 1, sr & 0x01);

    // Disassembly
    ImGui::SeparatorText("Disassembly");
    static std::string pre_context;
    static std::string disassembly;
    static std::string post_context;
    if (cpu_running == false) {
      static constexpr int kPreContext = 4;
      static constexpr int kPostContext = 7;

      static uint16_t last_pc = 0;
      if (last_pc != cpu_state.pc) {
        last_pc = cpu_state.pc;
        disassembler.set_instruction_boundary_hint(cpu_state.pc);
        QCHECK_OK(disassembler.disassemble());
        const auto& disassembly_vector = disassembler.disassembly();
        std::ranges::subrange pre_context_range(
            disassembly_vector.begin() + std::max(0, last_pc - kPreContext),
            disassembly_vector.begin() + last_pc);
        std::ranges::subrange post_context_range(
            disassembly_vector.begin() +
                std::min((int)disassembly_vector.size(), last_pc + 1),
            disassembly_vector.begin() +
                std::min((int)disassembly_vector.size(),
                         last_pc + kPostContext));
        pre_context = absl::StrJoin(pre_context_range, "");
        disassembly = disassembly_vector[last_pc];
        post_context = absl::StrJoin(post_context_range, "");
      }
    }
    ImGui::Text("%s", pre_context.c_str());
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s",
                       disassembly.c_str());
    ImGui::Text("%s", post_context.c_str());

    ImGui::SetCursorPosY(
        ImGui::GetWindowSize().y - ImGui::GetStyle().ItemSpacing.y -
        ImGui::GetStyle().FramePadding.y - ImGui::GetFrameHeightWithSpacing());
    if (ImGui::Button("Reset", ImVec2(-1, 0))) {
      absl::MutexLock lock(&callback_mutex_);
      cpu->reset();
      cpu_state = cpu->get_state();
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
    };
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
  }
  SDL_RemoveTimer(timer);

  // This makes sure that the timer callback has finished before we exit.
  absl::MutexLock lock(&callback_mutex_);

  return 0;
}
