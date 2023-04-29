# 6301 Software

This folder contains a monitor program and a number of 'user programs' that can
be launched from the monitor.

The include/ directory contains a number of utilities like keyboard handling,
ps2 decoding, code to talk to the serial or graphical terminal and more.

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

The monitor program goes at the end of the ROM. Technically it's put at `$f000`
(1k from the end) but programming erases the last 4k anyway as it needs to clear
a whole sector.

The user programs are put at the start of the ROM with the program registry at
`$8000` and the user programs after that.

You only need to issue one of `make monitor_prog` or `make programs_prog`
depending on which code you changed.

To program both ROM and monitor at once use `make prog`.

## TODOs and ideas

 * Make backspace work in the monitor.
 * Add serial input as an alternative way to get characters from `getchar`.
 * Add graphics save / restore commands to pico graphics to use for
   entering/exiting the monitor without interfering with user programs.
 * Hexdump for memory in the monitor.
 * Utility to print a number in decimal.
 * `include/*` code is linked twice right now, once into the monitor and once in
   user programs. Create a standard library for user programs which is just
   labels to the monitor versions.
 * A `help` command for the monitor.
