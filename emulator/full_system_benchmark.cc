#include <climits>
#include <cstdint>
#include <memory>
#include <sstream>
#include <vector>

#include "absl/log/globals.h"
#include "address_space.h"
#include "benchmark/benchmark.h"
#include "cpu6301.h"
#include "graphics.h"
#include "ram.h"
#include "rom.h"
#include "sd_card_spi.h"
#include "sound_opl3.h"
#include "spi.h"
#include "tl16c2550.h"
#include "w65c22.h"
#include "w65c22_to_spi_glue.h"

namespace eight_bit {
namespace {

constexpr uint16_t kProgramStart = 0x8000;
constexpr uint16_t kStackTop = 0x7E00;

// Full-system fixture replicating HD6301Thing's peripheral stack without the
// SDL event loop or emulator thread. Programs are loaded into ROM at 0x8000.
// Per-cycle tick callbacks (W65C22, W65C22ToSPIGlue) are registered exactly
// as in production.
struct FullSystemFixture {
  AddressSpace address_space;
  std::unique_ptr<Rom> rom;
  std::unique_ptr<Ram> ram;
  std::unique_ptr<Graphics> graphics;
  std::unique_ptr<Cpu6301> cpu;
  std::unique_ptr<SoundOPL3> sound_opl3;
  std::unique_ptr<TL16C2550> tl16c2550;
  std::unique_ptr<W65C22> w65c22;
  std::unique_ptr<W65C22ToSPIGlue> w65c22_to_spi_glue;
  std::unique_ptr<SPI> spi;
  std::unique_ptr<SDCardSPI> sd_card_spi;

  bool setup() {
    // ROM: 0x8000-0xFFFF (32KB)
    auto rom_or = Rom::create(&address_space, 0x8000, 0x8000);
    if (!rom_or.ok()) return false;
    rom = std::move(rom_or.value());

    // RAM: 0x0020-0x7EFF
    auto ram_or = Ram::create(&address_space, 0x0020, 0x7f00 - 0x0020);
    if (!ram_or.ok()) return false;
    ram = std::move(ram_or.value());

    // Graphics: 0x7FC0-0x7FDF (memory-mapped character display)
    auto graphics_or = Graphics::create(0x7fc0, &address_space);
    if (!graphics_or.ok()) return false;
    graphics = std::move(graphics_or.value());

    // CPU with internal registers at 0x0000-0x001F
    auto cpu_or = Cpu6301::create(&address_space);
    if (!cpu_or.ok()) return false;
    cpu = std::move(cpu_or.value());

    // OPL3 Sound: 0x7F80-0x7FFF
    auto sound_or = SoundOPL3::create(&address_space, 0x7f80);
    if (!sound_or.ok()) return false;
    sound_opl3 = std::move(sound_or.value());

    // TL16C2550 UART: 0x7F40-0x7F4F
    auto tl16c2550_or =
        TL16C2550::create(&address_space, 0x7f40, cpu->get_irq());
    if (!tl16c2550_or.ok()) return false;
    tl16c2550 = std::move(tl16c2550_or.value());

    // W65C22 VIA: 0x7F20-0x7F2F
    auto w65c22_or = W65C22::Create(&address_space, 0x7f20, cpu->get_irq());
    if (!w65c22_or.ok()) return false;
    w65c22 = std::move(w65c22_or.value());

    auto* w65c22_ptr = w65c22.get();
    cpu->register_tick_callback([w65c22_ptr]() { w65c22_ptr->tick(); });

    // W65C22-to-SPI glue logic
    auto glue_or = W65C22ToSPIGlue::create(
        w65c22_ptr->port_cb(), W65C22::kCb1Pin, w65c22->port_a(), 2,
        w65c22_ptr->port_ca(), W65C22::kCa1Pin, w65c22_ptr->port_b());
    if (!glue_or.ok()) return false;
    w65c22_to_spi_glue = std::move(glue_or.value());

    auto* glue_ptr = w65c22_to_spi_glue.get();
    cpu->register_tick_callback([glue_ptr]() { glue_ptr->tick(); });

    // SPI bus
    auto spi_or = SPI::create(
        w65c22->port_ca(), W65C22::kCa2Pin,
        w65c22_to_spi_glue->clk_out_port(), W65C22ToSPIGlue::kClkPin,
        w65c22->port_cb(), W65C22::kCb2Pin,
        w65c22_to_spi_glue->miso_port(), W65C22ToSPIGlue::kMisoPin);
    if (!spi_or.ok()) return false;
    spi = std::move(spi_or.value());

    // SD card on SPI bus (4MB empty image)
    auto stream = std::make_unique<std::stringstream>(
        std::string(4 * 1024 * 1024, '\0'),
        std::ios::in | std::ios::out | std::ios::binary);
    auto sd_or = SDCardSPI::create(spi.get(), std::move(stream));
    if (!sd_or.ok()) return false;
    sd_card_spi = std::move(sd_or.value());

    return true;
  }

  void load_program(std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> data(bytes);
    rom->load(0, data);
  }

  Cpu6301::TickResult run(uint16_t breakpoint) {
    Cpu6301::CpuState s = {
        .a = 0,
        .b = 0,
        .x = 0,
        .sp = kStackTop,
        .pc = kProgramStart,
        .sr = Cpu6301::StatusRegister(0).as_integer(),
        .breakpoint = breakpoint,
    };
    cpu->set_state(s);
    return cpu->tick(INT_MAX);
  }
};

// ---------------------------------------------------------------------------
// BM_FullSystem_RamCopy — Memory copy loop with full peripheral stack active.
// Every CPU cycle ticks the W65C22 VIA and SPI glue (via tick callbacks),
// plus the internal timer and serial. Direct comparison with BM_MemoryCopyLoop
// in cpu6301_benchmark.cc quantifies the full peripheral overhead.
//
// 0x8000: LDX #$0100     (CE 01 00)   3 cycles
// 0x8003: LDAA 0,X       (A6 00)      5 cycles
// 0x8005: STAA $80,X     (A7 80)      6 cycles
// 0x8007: INX            (08)         1 cycle
// 0x8008: CPX #$0200     (8C 02 00)   4 cycles
// 0x800B: BNE $8003      (26 F6)      3 cycles
// 0x800D: DECB           (5A)         1 cycle
// 0x800E: BNE $8000      (26 F0)      3 cycles
// 0x8010: breakpoint
// ---------------------------------------------------------------------------
void BM_FullSystem_RamCopy(benchmark::State& state) {
  FullSystemFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up full system");
    return;
  }
  f.load_program({
      0xCE, 0x01, 0x00,  // LDX #$0100
      0xA6, 0x00,        // LDAA 0,X
      0xA7, 0x80,        // STAA $80,X
      0x08,              // INX
      0x8C, 0x02, 0x00,  // CPX #$0200
      0x26, 0xF6,        // BNE -> 0x8003
      0x5A,              // DECB
      0x26, 0xF0,        // BNE -> 0x8000
  });
  for (auto _ : state) {
    auto result = f.run(0x8010);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_FullSystem_RamCopy);

// ---------------------------------------------------------------------------
// BM_FullSystem_GraphicsWrite — Writes characters to the graphics memory-mapped
// region (0x7FC0-0x7FDF). Each STAA triggers the Graphics peripheral's write
// callback. Runs with full peripheral tick callbacks active.
//
// 0x8000: LDX #$7FC0     (CE 7F C0)   3 cycles
// 0x8003: LDAA #$41      (86 41)      2 cycles
// 0x8005: STAA 0,X       (A7 00)      6 cycles
// 0x8007: INCA           (4C)         1 cycle
// 0x8008: INX            (08)         1 cycle
// 0x8009: CPX #$7FE0     (8C 7F E0)   4 cycles
// 0x800C: BNE $8005      (26 F7)      3 cycles
// 0x800E: DECB           (5A)         1 cycle
// 0x800F: BNE $8000      (26 EF)      3 cycles
// 0x8011: breakpoint
// ---------------------------------------------------------------------------
void BM_FullSystem_GraphicsWrite(benchmark::State& state) {
  FullSystemFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up full system");
    return;
  }
  f.load_program({
      0xCE, 0x7F, 0xC0,  // LDX #$7FC0
      0x86, 0x41,        // LDAA #$41
      0xA7, 0x00,        // STAA 0,X
      0x4C,              // INCA
      0x08,              // INX
      0x8C, 0x7F, 0xE0,  // CPX #$7FE0
      0x26, 0xF7,        // BNE -> 0x8005
      0x5A,              // DECB
      0x26, 0xEF,        // BNE -> 0x8000
  });
  for (auto _ : state) {
    auto result = f.run(0x8011);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_FullSystem_GraphicsWrite);

// ---------------------------------------------------------------------------
// BM_FullSystem_W65C22Timer — Configures the W65C22 timer 1 in free-run mode,
// then busy-polls reading the timer register for X iterations. Exercises the
// W65C22 tick callback path (timer decrement every cycle) plus memory-mapped
// I/O reads through AddressSpace dispatch.
//
// W65C22 registers (base 0x7F20):
//   T1C-L = 0x7F24, T1C-H = 0x7F25, ACR = 0x7F2B
//
// 0x8000: LDAA #$40      (86 40)      2 cycles — ACR bit 6 = free-run
// 0x8002: STAA $7F2B     (B7 7F 2B)   4 cycles — write ACR
// 0x8005: LDAA #$FF      (86 FF)      2 cycles — timer latch low = $FF
// 0x8007: STAA $7F24     (B7 7F 24)   4 cycles — write T1C-L
// 0x800A: STAA $7F25     (B7 7F 25)   4 cycles — write T1C-H (starts timer)
// 0x800D: LDAA $7F24     (B6 7F 24)   4 cycles — read T1C-L (poll loop)
// 0x8010: DEX            (09)         1 cycle
// 0x8011: BNE $800D      (26 FA)      3 cycles
// 0x8013: breakpoint
// ---------------------------------------------------------------------------
void BM_FullSystem_W65C22Timer(benchmark::State& state) {
  FullSystemFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up full system");
    return;
  }
  f.load_program({
      0x86, 0x40,        // LDAA #$40
      0xB7, 0x7F, 0x2B,  // STAA $7F2B (ACR)
      0x86, 0xFF,        // LDAA #$FF
      0xB7, 0x7F, 0x24,  // STAA $7F24 (T1C-L)
      0xB7, 0x7F, 0x25,  // STAA $7F25 (T1C-H, starts timer)
      0xB6, 0x7F, 0x24,  // LDAA $7F24 (read T1C-L — poll loop)
      0x09,              // DEX
      0x26, 0xFA,        // BNE -> 0x800D
  });
  for (auto _ : state) {
    Cpu6301::CpuState s = {
        .a = 0,
        .b = 0,
        .x = 10000,
        .sp = kStackTop,
        .pc = kProgramStart,
        .sr = Cpu6301::StatusRegister(0).as_integer(),
        .breakpoint = static_cast<uint16_t>(0x8013),
    };
    f.cpu->set_state(s);
    auto result = f.cpu->tick(INT_MAX);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_FullSystem_W65C22Timer);

}  // namespace
}  // namespace eight_bit

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
  SDL_Init(SDL_INIT_VIDEO);
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  SDL_Quit();
  return 0;
}
