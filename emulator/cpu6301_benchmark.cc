#include <array>
#include <climits>
#include <cstdint>
#include <memory>

#include "absl/log/globals.h"
#include "address_space.h"
#include "benchmark/benchmark.h"
#include "cpu6301.h"

namespace eight_bit {
namespace {

constexpr uint16_t kProgramStart = 0x0020;
constexpr uint16_t kStackTop = 0xF000;

// Minimal CPU environment for benchmarking. Creates an AddressSpace backed by
// a flat 64KB array and a Cpu6301 connected to it. The CPU's internal
// peripherals (timer, serial) tick every cycle as in production.
struct BenchmarkFixture {
  std::array<uint8_t, 65536> memory_data = {0};
  std::unique_ptr<AddressSpace> address_space;
  std::unique_ptr<Cpu6301> cpu;

  bool setup() {
    address_space = std::make_unique<AddressSpace>();
    auto cpu_or = Cpu6301::create(address_space.get());
    if (!cpu_or.ok()) return false;
    cpu = std::move(cpu_or.value());

    // The first 0x20 bytes are CPU-internal I/O registers already claimed by
    // Cpu6301::initialize().
    auto status = address_space->register_read(
        0x0020, 0xFFFF, [this](uint16_t addr) { return memory_data[addr]; });
    if (!status.ok()) return false;

    status = address_space->register_write(
        0x0020, 0xFFFF,
        [this](uint16_t addr, uint8_t data) { memory_data[addr] = data; });
    if (!status.ok()) return false;

    return true;
  }

  void load(uint16_t addr, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) {
      memory_data[addr++] = b;
    }
  }

  Cpu6301::TickResult run(uint16_t breakpoint, uint8_t a = 0, uint8_t b = 0,
                          uint16_t x = 0) {
    Cpu6301::CpuState s = {
        .a = a,
        .b = b,
        .x = x,
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
// BM_AddressSpaceGet — Isolated AddressSpace::get() microbenchmark.
// Measures the linear-scan dispatch cost per memory read.
// ---------------------------------------------------------------------------
void BM_AddressSpaceGet(benchmark::State& state) {
  std::array<uint8_t, 65536> mem = {0};
  AddressSpace as;
  as.register_read(0x0000, 0x3FFF, [&mem](uint16_t addr) {
      return mem[addr];
    }).IgnoreError();
  as.register_read(0x4000, 0x7FFF, [&mem](uint16_t addr) {
      return mem[addr];
    }).IgnoreError();
  as.register_read(0x8000, 0xBFFF, [&mem](uint16_t addr) {
      return mem[addr];
    }).IgnoreError();
  as.register_read(0xC000, 0xFFFF, [&mem](uint16_t addr) {
      return mem[addr];
    }).IgnoreError();

  uint16_t addr = 0x1000;
  for (auto _ : state) {
    benchmark::DoNotOptimize(as.get(addr));
    addr = (addr + 0x4001) & 0xFFFF;
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressSpaceGet);

// ---------------------------------------------------------------------------
// BM_NopLoop — Tight NOP loop measuring instruction dispatch overhead.
//
// Address  Bytes    Instruction
// 0x0020   01       NOP           (1 cycle)
// 0x0021   01       NOP           (1 cycle)
// 0x0022   01       NOP           (1 cycle)
// 0x0023   01       NOP           (1 cycle)
// 0x0024   09       DEX           (1 cycle)
// 0x0025   26 F9    BNE 0x0020    (3 cycles) [0x0020-(0x0025+2) = -7 = 0xF9]
// 0x0027   --       breakpoint
// ---------------------------------------------------------------------------
void BM_NopLoop(benchmark::State& state) {
  BenchmarkFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up CPU");
    return;
  }
  f.load(0x0020, {
                     0x01,  // 0x0020: NOP
                     0x01,  // 0x0021: NOP
                     0x01,  // 0x0022: NOP
                     0x01,  // 0x0023: NOP
                     0x09,  // 0x0024: DEX
                     0x26,
                     0xF9,  // 0x0025: BNE -> 0x0020
                 });
  for (auto _ : state) {
    auto result = f.run(0x0027, 0, 0, 10000);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_NopLoop);

// ---------------------------------------------------------------------------
// BM_ArithmeticLoop — ALU + direct/immediate addressing.
//
// Address  Bytes      Instruction
// 0x0020   96 80      LDAA $80     (3 cycles, direct)
// 0x0022   8B 01      ADDA #$01    (2 cycles, immediate)
// 0x0024   97 80      STAA $80     (4 cycles, direct)
// 0x0026   09         DEX          (1 cycle)
// 0x0027   26 F7      BNE 0x0020   (3 cycles) [0x0020-(0x0027+2) = -9 = 0xF7]
// 0x0029   --         breakpoint
// ---------------------------------------------------------------------------
void BM_ArithmeticLoop(benchmark::State& state) {
  BenchmarkFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up CPU");
    return;
  }
  f.load(0x0020, {
                     0x96,
                     0x80,  // 0x0020: LDAA $80
                     0x8B,
                     0x01,  // 0x0022: ADDA #$01
                     0x97,
                     0x80,  // 0x0024: STAA $80
                     0x09,  // 0x0026: DEX
                     0x26,
                     0xF7,  // 0x0027: BNE -> 0x0020
                 });
  for (auto _ : state) {
    f.memory_data[0x0080] = 0;
    auto result = f.run(0x0029, 0, 0, 10000);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_ArithmeticLoop);

// ---------------------------------------------------------------------------
// BM_MemoryCopyLoop — Indexed load/store stressing AddressSpace dispatch.
//
// Address  Bytes        Instruction
// 0x0020   CE 01 00     LDX #$0100   (3 cycles)
// 0x0023   A6 00        LDAA 0,X     (5 cycles, indexed)
// 0x0025   97 80        STAA $80     (4 cycles, direct)
// 0x0027   08           INX          (1 cycle)
// 0x0028   8C 02 00     CPX #$0200   (4 cycles)
// 0x002B   26 F6        BNE 0x0023   (3 cycles) [0x0023-(0x002B+2) = -10 =
// 0xF6] 0x002D   5A           DECB         (1 cycle) 0x002E   26 F0        BNE
// 0x0020   (3 cycles) [0x0020-(0x002E+2) = -16 = 0xF0] 0x0030   -- breakpoint
// ---------------------------------------------------------------------------
void BM_MemoryCopyLoop(benchmark::State& state) {
  BenchmarkFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up CPU");
    return;
  }
  for (int i = 0; i < 256; ++i) {
    f.memory_data[0x0100 + i] = static_cast<uint8_t>(i);
  }
  f.load(0x0020, {
                     0xCE,
                     0x01,
                     0x00,  // 0x0020: LDX #$0100
                     0xA6,
                     0x00,  // 0x0023: LDAA 0,X
                     0x97,
                     0x80,  // 0x0025: STAA $80
                     0x08,  // 0x0027: INX
                     0x8C,
                     0x02,
                     0x00,  // 0x0028: CPX #$0200
                     0x26,
                     0xF6,  // 0x002B: BNE -> 0x0023
                     0x5A,  // 0x002D: DECB
                     0x26,
                     0xF0,  // 0x002E: BNE -> 0x0020
                 });
  for (auto _ : state) {
    auto result = f.run(0x0030, 0, 100, 0);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_MemoryCopyLoop);

// ---------------------------------------------------------------------------
// BM_BranchHeavy — Nested countdown loops exercising branch resolution.
//
// Address  Bytes      Instruction
// 0x0020   86 FF      LDAA #$FF    (2 cycles)
// 0x0022   4A         DECA         (1 cycle)
// 0x0023   26 FD      BNE 0x0022   (3 cycles) [0x0022-(0x0023+2) = -3 = 0xFD]
// 0x0025   09         DEX          (1 cycle)
// 0x0026   26 F8      BNE 0x0020   (3 cycles) [0x0020-(0x0026+2) = -8 = 0xF8]
// 0x0028   --         breakpoint
// ---------------------------------------------------------------------------
void BM_BranchHeavy(benchmark::State& state) {
  BenchmarkFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up CPU");
    return;
  }
  f.load(0x0020, {
                     0x86,
                     0xFF,  // 0x0020: LDAA #$FF
                     0x4A,  // 0x0022: DECA
                     0x26,
                     0xFD,  // 0x0023: BNE -> 0x0022
                     0x09,  // 0x0025: DEX
                     0x26,
                     0xF8,  // 0x0026: BNE -> 0x0020
                 });
  for (auto _ : state) {
    auto result = f.run(0x0028, 0, 0, 100);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_BranchHeavy);

// ---------------------------------------------------------------------------
// BM_MixedWorkload — Realistic instruction mix: subroutine calls, stack ops,
// indexed memory access, and arithmetic. Sums 64 bytes via subroutine.
//
// Address  Bytes        Instruction
// 0x0020   7F 00 80     CLR $0080    (6 cycles, extended)
// 0x0023   7F 00 81     CLR $0081    (6 cycles, extended)
// 0x0026   CE 01 00     LDX #$0100   (3 cycles)
// 0x0029   BD 00 40     JSR $0040    (6 cycles, extended)
// 0x002C   08           INX          (1 cycle)
// 0x002D   8C 01 40     CPX #$0140   (4 cycles)
// 0x0030   26 F7        BNE 0x0029   (3 cycles) [0x0029-(0x0030+2) = -9 = 0xF7]
// 0x0032   5A           DECB         (1 cycle)
// 0x0033   26 EB        BNE 0x0020   (3 cycles) [0x0020-(0x0033+2) = -21 =
// 0xEB] 0x0035   --           breakpoint
//
// Subroutine at 0x0040:
// 0x0040   36           PSHA         (4 cycles)
// 0x0041   A6 00        LDAA 0,X     (5 cycles)
// 0x0043   9B 81        ADDA $81     (3 cycles, direct)
// 0x0045   97 81        STAA $81     (4 cycles, direct)
// 0x0047   96 80        LDAA $80     (3 cycles, direct)
// 0x0049   89 00        ADCA #$00    (2 cycles)
// 0x004B   97 80        STAA $80     (4 cycles, direct)
// 0x004D   32           PULA         (4 cycles)
// 0x004E   39           RTS          (5 cycles)
// ---------------------------------------------------------------------------
void BM_MixedWorkload(benchmark::State& state) {
  BenchmarkFixture f;
  if (!f.setup()) {
    state.SkipWithError("Failed to set up CPU");
    return;
  }
  for (int i = 0; i < 64; ++i) {
    f.memory_data[0x0100 + i] = static_cast<uint8_t>(i + 1);
  }
  f.load(0x0020, {
                     0x7F, 0x00, 0x80,  // 0x0020: CLR $0080
                     0x7F, 0x00, 0x81,  // 0x0023: CLR $0081
                     0xCE, 0x01, 0x00,  // 0x0026: LDX #$0100
                     0xBD, 0x00, 0x40,  // 0x0029: JSR $0040
                     0x08,              // 0x002C: INX
                     0x8C, 0x01, 0x40,  // 0x002D: CPX #$0140
                     0x26, 0xF7,        // 0x0030: BNE -> 0x0029
                     0x5A,              // 0x0032: DECB
                     0x26, 0xEB,        // 0x0033: BNE -> 0x0020
                 });
  f.load(0x0040, {
                     0x36,  // 0x0040: PSHA
                     0xA6,
                     0x00,  // 0x0041: LDAA 0,X
                     0x9B,
                     0x81,  // 0x0043: ADDA $81
                     0x97,
                     0x81,  // 0x0045: STAA $81
                     0x96,
                     0x80,  // 0x0047: LDAA $80
                     0x89,
                     0x00,  // 0x0049: ADCA #$00
                     0x97,
                     0x80,  // 0x004B: STAA $80
                     0x32,  // 0x004D: PULA
                     0x39,  // 0x004E: RTS
                 });
  for (auto _ : state) {
    f.memory_data[0x0080] = 0;
    f.memory_data[0x0081] = 0;
    auto result = f.run(0x0035, 0, 50, 0);
    benchmark::DoNotOptimize(result);
    state.counters["cycles"] = result.cycles_run;
  }
  state.SetItemsProcessed(state.iterations() * state.counters["cycles"]);
  state.counters["emulated_MHz"] = benchmark::Counter(
      state.counters["cycles"], benchmark::Counter::kIsIterationInvariantRate,
      benchmark::Counter::OneK::kIs1000);
}
BENCHMARK(BM_MixedWorkload);

}  // namespace
}  // namespace eight_bit

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
