# 6301 Software

This folder contains a monitor program and a number of 'user programs' that can
be launched from the monitor.

The `include/` directory contains a number of utilities like keyboard handling,
ps2 decoding, code to talk to the serial or graphical terminal and more.

## Assembler

The code and Makefiles here are written to use the
[AS](http://john.ccac.rwth-aachen.de:8000/as/) assembler. It's very
feature-packed, in active development, and one of the few that also supports the
4 extra instructions that the HD6301 introduced.

## Basic design

### Monitor program

The "monitor program" is responsible for basic hardware initialization, IRQ
handling, and providing various system-level routines for I/O etc. It also has
a basic [command line interface](#monitor-commands) and basic debugging functionality.

It's still called `monitor.s` because it started out as a traditional monitor
and then extended well past that. It gets to keep the name due to history.

The code is structured as a number of modules which each define
`[module_name]_init`, `[module_name]_irq` etc. as needed. These modules use
ASL's `SECTION` scoping concept that hides everything inside a section, unless
it's `PUBLIC` and/or `SHARED`. Things marked `PUBLIC` are visible to the monitor
code including them. Things that are marked as `SHARED`[^1] are also listed in a
`monitor.inc` file that's created during monitor assembly. Those are available
to "user programs" (as much as that makes sense on hardware that doesn't offer
any sort of support for such a separation).

Some modules are utility functions, some are specifically meant to initialize
various hardware boards (`sound.inc` for the OPL3 board, `midi_uart.inc` for the
MIDI board, etc.).

The `include` folder also contains a number of files defining useful constants
like e.g. register addresses, general midi instrument definitions.

Lastly it also contains a `macros.inc` that contains macros useful to coordinate
memory usage across the monitor and user programs. For example `zp_var name,
size` reserves `size` many bytes in the zero page and gives them the label
`label` guaranteeing that they won't conflict with zero page variables used by
the monitor. See that file for details.

[^1]: there is a `PUBLIC_SHARED` convenience macro that combines the two into
  one.

### User programs

"User programs" are defined in the `programs` directory and bundled together by
`programs.s`. That file also sets up the "program registry" which lists names
and start addresses for all programs in the ROM. That's the structure the
monitor programs `list` and `run` use to do their thing.

By convention, and to avoid conflicts, each program has a `SECTION` named after
itself, and exports only a `program_name_start` function outside of itself. This
makes it easier to bundle all programs together.

The separation between user code and monitor code makes it possible to flash
each part independently of each other and increase iteration speed.

If you're running in a user program you exit back to the monitor via the last
`rts`. You can also issue a software interrupt (the `swi` instruction) or hit
the 'break' key to get back into the monitor at any time.

> [!WARNING]
> After any change to monitor code it's prudent to reflash both monitor and
> programs. Chances are high that the addresses of monitor function have moved
> around. Old user code would jump to the wrong location and crash.

## I/O

Output routines of the "standard library" like `putchar`, `putstring` etc. work
both over serial and graphical output. Input can come from both serial and
keyboard. However note that there is no "break" key over serial - that requires
a physical keyboard to be connected.

There is an attempt to keep them roughly in sync, but there are some warts. For
example, commands that move the cursor around exist only in graphics mode. That
being said: look at `programs/snake.s` for how to do cursor movement over serial
too. Maybe at some point we'll put that into the standard library too.

This "dual stack" setup makes I/O a bit slow, but it's also very convenient. One improvement idea would be to make this selectable instead.

## Monitor commands

- `reset`: Run the monitor init routine, i.e. the code pointed to by the CPU
  reset vector.
- `run [program name]`: runs the named program. Short form: `r`.
- `list`: Lists the available programs.
- `continue`: If a user program is running, get back to it. Short form: `c`.
- `print`: If a user program is running print its current condition code
   register, A, B, X registers, stack pointer, and program counter. Short form: `p`.
- `ls`: If there is a FAT16 formatted SD card available, it lists the contents
  of the current directory.
- `cd [directory]`: change working directory. Only works one directory at a
  time, i.e. `cd foo/bar` doesn't work and needs to be separate `cd foo`, `cd
  bar`.

## Programs

See `programs.s` and the `programs/` folder. Follow the examples in those files
for how to add a new one. You basically need to both include the program in
`programs.s` and also add it to the program registry in that same file.

## Memory map

See the comment at the start of `include/memory_map.inc`.

## Programming the ROM

There are two `Makefile` targets: `monitor_prog` and `programs_prog` which will
assemble the relevant piece of code (if needed) and write it at the appropriate
location in the ROM.

The monitor program goes at the end of the ROM. It's designed to go into the
last 8k bytes, starting at `$e000`. This usually wastes a bit of space between
monitor code and the vectors starting at `$fff0`, but the flash chip has pages
of 4k size. Until we start to run low on storage this makes no difference to
programming speed.

The user programs are put at the start of the ROM with the program registry at
`$8000` and the user programs after that.

You only need to issue one of `make monitor_prog` or `make programs_prog`
depending on which code you changed. If you intend to run programs besides the
monitor after changing the monitor you need to reprogram both as the library
addresses will have moved around. To program both at once use `make prog`.

## TODOs and ideas for improvements

- Add graphics save / restore commands to pico graphics to use for
   entering/exiting the monitor without interfering with user programs.
- Hexdump for memory in the monitor.
- A `help` command for the monitor.
- Make I/O over graphics, serial, keyboard or all of them selectable at compile
  or runtime to avoid any speed penalties.

### Project: Making SD Card reads *fast*

SD card reads are currently relatively slow. Reading a single page currently
takes ~120ms according to the `sdbench.s` program run in the emulator and
assuming 1MHz clock. This is much slower than a theoretical target of
e.g. reading one bit per cycle, which would take 8µs * 512 = 4096µs or ~4ms.

There are multiple ways to address this, from faster software to better hardware
tricks like using a wider bus, offloading some SPI processing etc.

#### Bit-banging approach

For record keeping, the optimizations so far - in `sdbench.s` numbers - are:

- `57058 01`, baseline. That's 57058 + 65536 = 122594 cycles, or ~122ms.
- `51426 01`, saved 6ms by inlining the hot SPI byte reads.
- `30221 01`, saved 21ms by unrolling the 8 bit loop and saving `pushx`/`pulx`
  pairs.
- `22029 01`, saved 8ms by using the fact that CLK is bit 0 and `inc b` / `dec
  b` is 2 cycles shorter than two `eor b,#CLK` to raise / lower the clock.
- `25884 01`, lost 4ms again by choosing to not inline the `spi_receive_byte`
  but moving the above optimizations there. Cleaner code wins over a small speed
  bump. Total now at ~91ms, or a 25% improvement.

The current bitbang code for each bit is this:

```asm
        inc b                   ; 1 cycle
        stb IO_ORA              ; 4 cycles, raise clock
        asl a                   ; 1 cycle
        tst IO_IRA              ; 4 cycles, sets N to PA7
        bpl +                   ; 3 cycles, On 0 we don't need the 'inc a'
        inc a                   ; 1 cycle
+       dec b                   ; 1 cycle
        stb IO_ORA              ; 4 cycles, Lower clock
```

It doesn't look like it can be simplified much, and it's 19 cycles long. With a
few extra instructions for setup that's 160 cycles per byte. Assuming no extra
cost that works out to ~81ms per sector as a hard limit.

#### Shift-register approach

The W65C22 has a shift register that can work as fast as half the system
clock. That's potentially 16 cycles per byte plus code to read them. CB1 outputs
a clock signal, CB2 receives input data. The clock is high when idle, data is
supposed to be prepared on the falling edge, and the W65C22 samples it on the
rising clock edge.

The issue is that SD Cards speak SPI mode 0, which requires a clock that's low
when idle, and samples on the rising edge. Inverting the '22 clock makes it low
on idle, but the sampling happens on the wrong edge.

For communication from the '22 to the SD card one can invert the clock and delay
it by ~1 cycle to make it work.

For communication from the SD card to the '22 we can use something like a
74xx164 shift register for serial to parallel conversion and connect the 8 bits
to a full port. It's clocked the same way as the write side.

Sending data is then a write into the SR + 17 cycles to clock it out (accounting
for the clock delay), so 21 cycles. Receiving data requires a write into the SR
to get the clock going, then a 17 cycles delay, then a read on the port, which
is 25 cycles total.
