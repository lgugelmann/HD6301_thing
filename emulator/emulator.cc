#include <SDL3/SDL.h>

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
#include "cpu6301.h"
#include "graphics.h"
#include "hd6301_thing.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

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

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Initialize SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    LOG(FATAL) << absl::StreamFormat("Failed to initialize SDL: %s",
                                     SDL_GetError());
  }
  absl::Cleanup sdl_cleanup([] { SDL_Quit(); });

  // Avoid the SDL window turning the compositor off
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    LOG(ERROR) << "SDL can not disable compositor bypass - not running under "
                  "Linux?";
  }

  // Make sure to scale up the window for high DPI displays
  float scale = 1.0f;
  int num_displays = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&num_displays);
  if (!displays) {
    LOG(ERROR) << absl::StreamFormat("Failed to enumerate displays: %s",
                                     SDL_GetError());
  } else {
    // We arbitrarily pick the first display
    scale = SDL_GetDisplayContentScale(displays[0]);
    SDL_free(displays);
    // Round up to next higher integer. We're doing pixel graphics, that doesn't
    // look good at fractional scaling.
    scale = std::ceil(scale);
    printf("Scaling by %f\n", scale);
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer(
          "Emulator", (kGraphicsFrameWidth + kDebugWindowWidth) * scale,
          kGraphicsFrameHeight * scale, 0, &window, &renderer)) {
    LOG(FATAL) << absl::StreamFormat("Failed to create window and renderer: %s",
                                     SDL_GetError());
  }
  absl::Cleanup window_cleanup([window] { SDL_DestroyWindow(window); });
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

  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  absl::Cleanup imgui_sdl_cleanup([] { ImGui_ImplSDL3_Shutdown(); });
  ImGui_ImplSDLRenderer3_Init(renderer);
  absl::Cleanup imgui_renderer_cleanup(
      [] { ImGui_ImplSDLRenderer3_Shutdown(); });

  auto hd6301_thing =
      eight_bit::HD6301Thing::create(absl::GetFlag(FLAGS_ticks_per_second),
                                     eight_bit::HD6301Thing::kKeyboard65C22);
  QCHECK_OK(hd6301_thing);

  // Prepare ROM image file
  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "No ROM file specified.";
  const std::string rom_file_name = absl::GetFlag(FLAGS_rom_file);
  std::ifstream rom_file(rom_file_name, std::ios::binary);
  QCHECK(rom_file.is_open()) << "Failed to open file: " << rom_file_name;
  std::vector<uint8_t> rom_data(std::istreambuf_iterator<char>(rom_file), {});
  constexpr uint rom_size = 0x8000;
  constexpr uint rom_start = 0x8000;
  uint16_t rom_load_address = rom_size - rom_data.size();
  (*hd6301_thing)->load_rom(rom_load_address, rom_data);

  // Prepare SD card image file, if provided
  if (!absl::GetFlag(FLAGS_sd_image_file).empty()) {
    const std::string image_file_name = absl::GetFlag(FLAGS_sd_image_file);
    bool persist_writes = absl::GetFlag(FLAGS_sd_image_persist_writes);
    auto file_stream = std::make_unique<std::fstream>();
    file_stream->open(std::string(image_file_name),
                      std::ios::in | std::ios::out | std::ios::binary);
    QCHECK(file_stream->is_open())
        << "Failed to open file: " << image_file_name;
    if (persist_writes) {
      LOG(WARNING) << "Persisting writes to the SD card image file";
      (*hd6301_thing)->load_sd_image(std::move(file_stream));
    } else {
      auto string_stream = std::make_unique<std::stringstream>();
      (*string_stream) << file_stream->rdbuf();
      file_stream->close();
      (*hd6301_thing)->load_sd_image(std::move(string_stream));
    }
  }

  eight_bit::Disassembler disassembler;
  QCHECK_OK(disassembler.set_data(rom_start + rom_load_address, rom_data));
  QCHECK_OK(disassembler.disassemble());

  (*hd6301_thing)->run();

  // Main loop
  SDL_Event event;
  bool running = true;
  while (running) {
    // Handle events
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);

      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        // Only pass keyboard events if Dear ImGui doesn't want them
        if (!io.WantCaptureKeyboard) {
          (*hd6301_thing)->handle_keyboard_event(event.key);
        }
      }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
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
    static bool cpu_running = true;

    cpu_running = (*hd6301_thing)->is_cpu_running();
    if (ImGui::Button(button_text.c_str(), ImVec2(-1, 0))) {
      if (cpu_running) {
        (*hd6301_thing)->stop();
        cpu_running = false;
      } else {
        // If we're paused we might be on a breakpoint and the program would
        // immediately stop again on the same instruction. Make sure we move
        // one instruction past the breakpoint.
        (*hd6301_thing)->tick(1, true /* ignore_breakpoint */);
        (*hd6301_thing)->run();
        cpu_running = true;
      }
    }
    // Note: the CPU could be stopped by the user or by hitting a breakpoint,
    // not just the button above.
    if (cpu_running != ui_in_running_state) {
      if (cpu_running) {
        button_text = "Pause";
      } else {
        button_text = "Run";
        cpu_state = (*hd6301_thing)->get_cpu_state();
      }
      ui_in_running_state = cpu_running;
    }
    ImGui::BeginDisabled(cpu_running);
    if (ImGui::Button("Step", ImVec2(-1, 0))) {
      (*hd6301_thing)->tick(1, true /* ignore_breakpoint */);
      cpu_state = (*hd6301_thing)->get_cpu_state();
    }

    // Breakpoint handling
    ImGui::SeparatorText("Breakpoints");
    static std::string breakpoint_str("0000");
    ImGui::InputText("Address", breakpoint_str.data(),
                     breakpoint_str.size() + 1,
                     ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::Button("Set breakpoint", ImVec2(-1, 0))) {
      // SimpleAtoi only works with 32 or 64 bit ints.
      int breakpoint;
      if (absl::SimpleHexAtoi(breakpoint_str, &breakpoint) && breakpoint >= 0 &&
          breakpoint <= 0xffff) {
        (*hd6301_thing)->set_breakpoint((uint16_t)breakpoint);
        cpu_state = (*hd6301_thing)->get_cpu_state();
      } else {
        // Bare-bones error handling as the
        // ImGuiInputTextFlags_CharsHexadecimal above should prevent an
        // invalid address from being settable.
        LOG(ERROR) << "Invalid breakpoint address: " << breakpoint_str;
      }
    }
    if (cpu_state.breakpoint.has_value()) {
      ImGui::Text("Breakpoint set at 0x%04X", cpu_state.breakpoint.value());
      ImGui::SameLine();
      if (ImGui::Button("Clear breakpoint")) {
        (*hd6301_thing)->clear_breakpoint();
        cpu_state = (*hd6301_thing)->get_cpu_state();
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
      constexpr int kPreContext = 4;
      constexpr int kPostContext = 7;

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
    }
    if (ImGui::Button("Reset", ImVec2(-1, 0))) {
      (*hd6301_thing)->reset();
      cpu_state = (*hd6301_thing)->get_cpu_state();
    }
    if (show_ram_hexdump) {
      static std::string ram_hexdump;
      static uint16_t last_pc = 0;
      if (last_pc != cpu_state.pc) {
        last_pc = cpu_state.pc;
        ram_hexdump = (*hd6301_thing)->get_ram_hexdump();
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
    SDL_FRect graphics_rect = {0, 0, kGraphicsFrameWidth * scale,
                               kGraphicsFrameHeight * scale};
    auto status = (*hd6301_thing)->render_graphics(renderer, &graphics_rect);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to render graphics: " << status;
      break;
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }
  return 0;
}
