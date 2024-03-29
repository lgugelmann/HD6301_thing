# 6301 Software

This folder contains a monitor program and a number of 'user programs' that can
be launched from the monitor.

The include/ directory contains a number of utilities like keyboard handling,
ps2 decoding, code to talk to the serial or graphical terminal and more.

## Assembler

The code and Makefiles here are written to use the
[AS](http://john.ccac.rwth-aachen.de:8000/as/) assembler. It's very
feature-packed, in active development, and one of the few that also supports the
4 extra instructions that the HD6301 introduced.

## Monitor

See `monitor.s`. It runs at startup and has a simple command line interface
which supports a few basic commands. It outputs to both the graphical terminal
and serial. It only takes input from the ps2 keyboard however.

If you're running in a user program you can either issue a software interrupt
(the `swi` instruction) or hit the 'break' key to get back into the monitor.

### Monitor commands

 * `reset`: as the name says, start everything from scratch.
 * `run [program name]`: runs the named program. Short form: `r`.
 * `ls`: lists the available programs.
 * `continue`: if a user program is running, get back to it. Short form: `c`.
 * `print`: if a user program is running print its current condition code
   register, A, B, X registers, stack pointer, and program counter. Short form: `p`.

## Programs

See `programs.s` and the `programs/` folder. Follow the examples in those files
for how to add a new one. You baiscally need to both include the program in
`programs.s` and also add it to the program registry in that same file.

## Memory map

See the comment at the start of `monitor.s`. This needs to be defined better.

## Programming the ROM

There are two `Makefile` targets: `monitor_prog` and `programs_prog` which will
assemble the relevant piece of code (if needed) and write it at the appropriate
location in the ROM.

The monitor program goes at the end of the ROM. It's put at `$f000` (4k from the
end) as the programmer insists on flashing at sector boundaries
only. Technically it's much smaller than that.

The user programs are put at the start of the ROM with the program registry at
`$8000` and the user programs after that.

You only need to issue one of `make monitor_prog` or `make programs_prog`
depending on which code you changed. If you intend to run programs besides the
monitor after changing the monitor you need to reprogram both as the library
addresses will have moved around. To program both at once use `make prog`.

## TODOs and ideas for improvements

 * Make backspace work in the monitor.
 * Add serial input as an alternative way to get characters from `getchar`.
 * Add graphics save / restore commands to pico graphics to use for
   entering/exiting the monitor without interfering with user programs.
 * Hexdump for memory in the monitor.
 * A `help` command for the monitor.
 * Make the memory map more explict and document it to avoid monitor / user
   program conflicts. The `include/macros.inc` macros don't help anymore as
   monitor and user programs are assembled separately.
   * Get the keyboard buffer away from user RAM.
   * Make it clear which zeropage addresses are system vs user usable.
