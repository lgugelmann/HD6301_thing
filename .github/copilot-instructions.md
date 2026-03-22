# HD6301V1 Retro Computer — Copilot Instructions

A modular retro computer built around the Hitachi HD6301V1 (1 MHz, 8-bit, Motorola 6801 derivative). Multiple boards connect via a 40-pin ribbon cable carrying a 16-bit address bus, 8-bit data bus, clock, R/W, interrupt, and reset lines.

## Repository Map

| Directory | Purpose |
|-----------|---------|
| `emulator/` | C++23 full-system emulator — CPU, peripherals, graphics, sound, SD card. Main development/test environment. See `emulator/README.md`. |
| `asm/` | HD6301 assembly: monitor OS (`monitor.s`), standard library (`stdlib.inc`), ~19 user programs. Built with the ASL assembler. See `asm/README.md`. |
| `disassembler/` | HD6301 binary disassembler tool and library. Worklist-based recursive disassembly over 64 KB address space. |
| `graphics_state/` | Shared C++ graphics library used by both the emulator and Pico firmware. 800×600 text mode, 100×40 character grid. See `graphics_state/README.md`. |
| `pico_graphics/` | Raspberry Pi Pico firmware for DVI/HDMI graphics output (800×600). Uses PicoDVI. |
| `pico_programmer/` | Pico firmware + Python host script for USB flash programming of the CPU board. |
| `io_board/` | I/O expansion board: W65C22 VIA, SPI, PS/2 keyboard. Includes GreenPAK configs. |
| `board/` | KiCad PCB design for the main HD6301V1 CPU board. |
| `board_template/` | KiCad template for new expansion boards. |
| `programmer_board/` | KiCad PCB design for the Pico programmer board. |
| `greenpak/` | GreenPAK programmable logic configs — address decoding, protocol bridging, timing. |
| `opl3_sound/` | OPL3 FM synthesis audio board support and VGM test files. |
| `midi/` | MIDI board support, sample files, and analysis scripts. |
| `sd_card/` | Pre-built FAT16 SD card image. |
| `internal_rom/` | Dumped and disassembled HD6301 mask ROM. |
| `tools/` | Utility scripts: FAT16 image builder, MIDI parsers, format converters. |
| `dependencies/` | Git submodules: Abseil, GoogleTest, Dear ImGui, Nuked-OPL3, PicoDVI, uni_vga font. |
| `documentation/` | HD6301/HD6303 instruction set reference (AI-generated, not yet fully validated). |

## Emulator Architecture

The emulator lives in `emulator/` and models the full hardware stack. Entry point: `emulator.cc`.

### Key Classes

| Class | File | Role |
|-------|------|------|
| `HD6301Thing` | `hd6301_thing.{h,cc}` | Top-level orchestrator. Creates CPU, memory, and all peripherals; runs the main emulation loop. |
| `Cpu6301` | `cpu6301.{h,cc}` | HD6301 CPU core. Instruction execution, registers (A, B, D, X, SP, PC, SR), interrupts, breakpoints. |
| `AddressSpace` | `address_space.{h,cc}` | 16-bit memory bus. Routes reads/writes to registered callbacks for memory-mapped I/O. |
| `RAM` / `ROM` | `ram.{h,cc}`, `rom.{h,cc}` | Memory blocks. ROM loads from binary files. |
| `W65C22` | `w65c22.{h,cc}` | Versatile Interface Adapter — timers, I/O ports A/B, shift register, interrupt control. |
| `W65C22ToSpiGlue` | `w65c22_to_spi_glue.{h,cc}` | Glue logic between W65C22 port pins and SPI byte interface. |
| `SPI` | `spi.{h,cc}` | Bit-banged SPI adapter (IOPort ↔ byte-level SPI devices). |
| `SDCardSPI` | `sd_card_spi.{h,cc}` | Minimal SDHC card emulation on the SPI bus. |
| `TL16C2550` | `tl16c2550.{h,cc}` | Dual UART with PTY pseudo-terminal support. |
| `PS2Keyboard6301` | `ps2_keyboard_6301.{h,cc}` | PS/2 keyboard via 8-bit IOPort with interrupt signaling. |
| `SoundOPL3` | `sound_opl3.{h,cc}` | OPL3 FM synthesis via Nuked-OPL3, output through SDL audio. |
| `Graphics` | `graphics.{h,cc}` | Character display rendering with color palette, driven by SDL. |
| `HD6301Serial` | `hd6301_serial.{h,cc}` | HD6301 built-in serial module. |
| `MidiToSerial` | `midi_to_serial.{h,cc}` | Optional RtMidi virtual port → PTY bridge. |
| `IOPort` | `ioport.{h,cc}` | 8-bit I/O port simulation shared by all peripherals. |
| `Timer` | `timer.{h,cc}` | HD6301 internal timer. |

### Data Flow

1. `Cpu6301` reads/writes through `AddressSpace`.
2. `AddressSpace` dispatches to the correct peripheral via registered address-range callbacks.
3. Peripherals (`W65C22`, `TL16C2550`, `Graphics`, `SoundOPL3`) update internal state.
4. `Graphics` renders each frame via SDL.
5. `SoundOPL3` generates audio samples to SDL AudioStream.
6. Input (keyboard, MIDI) raises interrupts back to the CPU via the `Interrupt` controller.
7. SPI traffic flows: `W65C22` → `W65C22ToSpiGlue` → `SPI` → `SDCardSPI`.

### Threading Model

- CPU execution is single-threaded (instruction tick loop).
- Peripherals use mutexes for thread-safe access (graphics state, audio, interrupts, UART).
- `TL16C2550` spawns a read thread for PTY input.
- Main thread: SDL event loop, rendering, audio callback.

## Build System

**Root CMake** (`CMakeLists.txt`): C++23, optional ThreadSanitizer. Subdirectories:

```
dependencies/  →  Abseil, GoogleTest, ImGui, Nuked-OPL3
asm/           →  ASL assembler targets (monitor.bin, programs.bin, rom.bin)
io_board/      →  SD card image generation
disassembler/  →  disassembler executable + disassembler_lib
emulator/      →  emulator executable + supporting libs
```

**Host dependencies**: SDL3, optional RtMidi, optional gperftools.

**Building the emulator**:
```bash
cd build && cmake .. && cmake --build .
```

**Building the ROM** (requires ASL assembler):
```bash
cd asm && make rom
```

## Assembly Conventions (`asm/`)

- **Assembler**: ASL (Macro Assembler AS) — supports HD6301-specific instructions (AIM, OIM, EIM, TIM).
- **Scoping**: ASL `SECTION` with `PUBLIC`/`SHARED` symbol exports.
- **Memory layout**: Monitor at `$D000`, user programs at `$8000`+.
- **Program registration**: `generate_programs_list.py` auto-discovers programs in `asm/programs/` and writes `programs.lst` / `programs.s`.
- **Include hierarchy**: `stdlib.inc` → `monitor.inc` → individual device includes in `asm/include/`.
- **Key includes**: `memory_map.inc` (address layout), `registers.inc` (CPU/peripheral registers), `macros.inc` (common macros).

## Testing

GoogleTest is used throughout. Test files:

| Test File | What It Tests |
|-----------|---------------|
| `emulator/testdata/instructions/*.test` | **Declarative CPU instruction tests** — one `.test` file per instruction (~120 files, ~314 cases) |
| `emulator/cpu6301_test.cc` | CPU test fixture (`Cpu6301Test`) for non-instruction tests |
| `emulator/w65c22_test.cc` | W65C22 VIA: ports, timers, interrupts |
| `emulator/w65c22_to_spi_glue_test.cc` | Glue logic between W65C22 and SPI |
| `emulator/spi_test.cc` | SPI bit-banging protocol |
| `emulator/hexdump_test.cc` | Hex dump formatting |
| `disassembler/disassembler_test.cc` | Disassembler correctness |
| `graphics_state/graphics_state_test.cc` | Graphics state and rendering logic |

Test targets: `emulator_tests` (C++ tests), `instruction_tests` (declarative `.test` files).

Run tests:
```bash
cd build && ctest
```

### Declarative Instruction Tests

CPU instruction tests use a declarative `.test` file format in `emulator/testdata/instructions/`. Each file is named after its instruction (e.g., `adda.test`, `bne.test`, `lsr.test`). The C++ infrastructure is in `instruction_test_parser.{h,cc}` (parser) and `instruction_test_runner.cc` (GoogleTest parameterized runner).

**File format** — each `.test` file contains one or more test cases separated by `---`:

```
# Comments start with #

---
name: descriptive test name
memory: 8B 01           # instruction bytes placed at PC (hex)
setup: a=0F x=1000 C=1  # initial CPU state (key=value, space-separated)
at 0042: 12 34           # pre-load memory at address (hex bytes)
expect: a=10 N=0 Z=0    # expected register/flag values after execution
expect_at 1010: FF       # expected memory byte at address
cycles: 2                # expected cycle count
```

**Fields:**

| Field | Required | Description |
|-------|----------|-------------|
| `name:` | Yes | Test case name (used in GoogleTest output) |
| `memory:` | Yes | Hex bytes of the instruction, placed at PC |
| `setup:` | No | Initial register/flag values: `a`, `b`, `x`, `sp`, `pc`, `sr`, `H`, `I`, `N`, `Z`, `V`, `C` |
| `at XXXX:` | No | Pre-load hex bytes at address XXXX (can appear multiple times) |
| `expect:` | No | Expected values after execution — same keys as `setup` |
| `expect_at XXXX:` | No | Expected single byte at address XXXX (can appear multiple times) |
| `cycles:` | No | Expected cycle count |

**Defaults** (if not specified in `setup`): `a=00, b=00, x=0000, sp=F000, pc=0020, sr=C0` (bits 7–6 always set). Only fields present in `expect` are checked — omitted fields are not asserted.

**Adding a new instruction test:** Create `emulator/testdata/instructions/<mnemonic>.test`. It will be auto-discovered by the test runner. Cover at minimum: basic operation, all relevant addressing modes (IMM/DIR/IDX/EXT), flag effects, and edge cases (zero, overflow, carry).

**Example** (`suba.test`):
```
# SUBA - Subtract from A
# Immediate: 0x80, Direct: 0x90, Indexed: 0xA0, Extended: 0xB0

---
name: immediate
memory: 80 05
setup: a=10
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 2

---
name: direct
memory: 90 42
setup: a=10
at 0042: 05
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 3
```

## Running the Emulator

```bash
./build/emulator/emulator --rom_file=build/asm/rom.bin [--sd_image_file=sd_card/sd_card.img]
```

Available monitor commands: `list`, `run <program>`, `reset`, `clear`, `continue`, `print`, `ls`, `cd`, `cat`.

## Code Style

- `.clang-format` in repo root — use it.
- C++23 standard.
- Abseil for flags (`absl::Flag`), logging (`LOG`), status (`absl::Status`), and synchronization (`absl::Mutex`).
- Header guards use `#ifndef` / `#define` style.
- Test naming: `TEST(SuiteName, DescriptiveTestName)`.
