# HD6301-thing emulator

The goal of this emulator is to be able to run the asm code in the `asm` folder
with a reasonable degree of accuracy, including graphics.

## Features

Currently the emulator has:

- A CPU core emulating most of the HD6301V1 processor
  - I/O ports 1 and 2 are available. Port 2 is actually 8 bits instead of 5.
  - Port 2 is only used for I/O and not mode selection (not implemented).
  - The Serial Communication Interface is not implemented.
  - The timer only supports counting / overflow and none of the P2 modes.
  - Only reset, irq, and timer interrupt vectors are used.
  - A few instructions aren't implemented yet: `daa`, `slp`, `wai` and `swi`.
- A PS2 keyboard emulation. Ish, it's one way only, and it's 8 bit parallel connected to port1 of the CPU instead of serial.
- Emulation for the `pico_graphics` graphics board

Missing are:

- OPL3 board
- Any kind of serial comms, including the MIDI board

## How to compile

The emulator depends on libSDL and the Abseil C++ libraries. It can be built using CMake. Create a build somewhere, e.g. `emulator/build`. Then in that directory run `cmake [path to CMakeList.txt]`. In the example before that's `cmake ..`. After successfully configuring, run `make` to create the `emulator` binary.

## Creating a ROM file to run

In the `asm` directory run `make rom` to create a combined monitor / programs
rom.

## Running the emulator

```
./emulator --rom_file=[rom file]
```
