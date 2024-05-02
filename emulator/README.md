# HD6301-thing emulator

The goal of this emulator is to be able to run the asm code in the `asm` folder
with a reasonable degree of accuracy, including graphics, sound, and I/O.

## Features

The emulator has code for all hardware boards in the rest of this repository
with enough accuracy that it can run all current code. In particular:

- A CPU core emulating most of the HD6301V1 processor
  - I/O ports 1 and 2 are available. Port 2 is actually 8 bits instead of 5, and
    it doesn't share pins with the SCI.
  - Mode selection doesn't happen. The CPU is always in mode 2/4.
  - The SCI only implements internal clock sources, and basic rx/tx. It doesn't
    do overrun and framing checks. It doesn't implement wake-up either.
  - The timer only supports counting / overflow and none of the P2 modes.
  - Only reset, irq, SCI, and timer interrupt vectors are used.
  - A few instructions aren't implemented yet: `daa`, `slp`, `wai` and `swi`.
- A PS2 keyboard emulation. Ish, it's one way only, and it's 8 bit parallel
  connected to port1 of the CPU instead of serial (like the actual hardware).
- Full emulation for the `pico_graphics` graphics board
  - Some timing might be off. All operations in the emulator complete in one
    tick, the real hardware is a bit slower in some cases (though I never spent
    time characterizing this exactly, so who knows?)
- OPL3 chip emulation courtesy of the Nuked-OPL3 emulator.
  - Due to the way sound is generated in batches, the OPL3 commands might be
    slighly misaligned vs actual hardware.
- TL16C2550 emulation for the MIDI board. This one is really basic still and
  most of the features aren't there. Proper baud rate, buffers, etc. are not
  implemented. The second UART isn't implemented either.
- Bare-bones WD65C22 VIA emulation (only port A/B)
- Bare-bones emulation of an SD card SPI interface attached to WD65C22 port A

The serial interfaces can be interacted with by reading/writing on the PTYs
printed on the console when the emulator starts.

There is an optional dependency on RtMidi. If it's available, then a virtual
MIDI input port is created that makes it easier to talk to the emulated MIDI
board.

## How to compile

The emulator has a hard dependency on SDL2, and an optional one on RtMidi. It
can be built using CMake. Create a build directory somewhere, e.g.
`emulator/build`. Then in that directory run `cmake [path to CMakeList.txt]`. In
the example before that's `cmake ..`. After successfully configuring, run `make`
to create the `emulator` binary.

Alternatively, open the project in VSCode and hit F7 (assuming you have the
CMake extensions installed - which is very likely).

NOTE: The CMake setup is not particularly great. For example it doesn't set up
  minimum versions for any libraries used.

## Creating a ROM file to run

In the `/asm` directory run `make rom` to create a combined monitor / programs
rom (called `/asm/rom.bin`). You can also create just the monitor ROM (`make
monitor`), or the programs ROM. However note that the latter can't run without
the monitor one though.

See the documentation there for the prerequisites for assembling a ROM image.

## Running the emulator

```sh
./emulator --rom_file=[rom file]
```

There are a few extra command line options. To see them, run `./emulator
--helpfull`. There are somewhat verbose `VLOG()` statements sprinkled in the
codebase for some debugging help, levle 3 and above prints every instruction
that's being run for example. For that, add `-v 3 --stderrthreshold 0` to the
command line for example.

## Things to try

After you start the ROM file you'll see the monitor prompt like this:

```text

> 
```

Try `ls` to see a list of available programs. You can run them with `run` or `r`
followed by the program name. For example:

```text
> ls
edi 94B7
graphics_test 9A5E
hello 9B21
keyboard_test 9B5D
midi_synth 9B80
opl3_test 9EAC
random A055
sci_echo A071
seq A2ED
serial_opl3 A5FF
snake A664
test AA2C
volume_test AA4D

> run graphics_test
```

runs a basic graphics test and drops you back into the monitor.

To get back to the monitor from any program, hit the 'Pause / Break' key on your
keyboard. You'll see a dump of the status register flags, the A, B, X registers,
the program counter, and the stack pointer, and then the familiar monitor prompt.

```text
--HINZVC  A  B    X   PC   SP
11000100 00 00 100B F89B 7DF2
> 
```

To get back to the running program use the `continue` or `c` command.

Other available commands are `reset` which starts the system fresh, and `clear`
which resets the graphics. That last command may also bring back the cursor.
Some programs disable it and don't turn it back on on exit :-).

## Profiling & sanitizer runs

### Profiling

There is some support for profiling using
[gperftools](https://github.com/gperftools/gperftools). To do so turn on the
`GPERFTOOLS_BUILD` option (e.g. by using `ccmake .` in the build folder) and
recompile.

To get a profile run the emulator with:

```sh
CPUPROFILE=cpu.profile ./emulator --rom_file=../asm/rom.bin
```

You can then examine the profile with e.g. `pprof` using:

```sh
pprof -http localhost:8080 ./emulator cpu.profile
```

### Sanitizers

To run a ThreadSanitizer build enable the `TSAN_BUILD` option (see above for
how) and rebuild.

Running the binary now prints warnings about potential threading issues.

Note: On my system TSan flags a false positive with audio buffers passing
  between SDL and Pulseaudio.
