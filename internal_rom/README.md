# Dumping the internal ROM of an HD6301V1

## Hardware setup

The MCU's test mode (mode 0) maps the internal ROM to the last 4k of the address
space. It has a special handling of the reset vector locations `FFFE` and
`FFFF`. During the first 4 cycles after `RESETB` goes high those locations
aren't read from the internal ROM but can be supplied externally. This means
that they don't get read from the internal ROM and we can send the MCU to
execute any arbitrary code we want.

One way to dump the internal ROM is to put some ROM dumping code into the lower
4K of an 8K ROM, map that ROM to the top of the address space, and ensure that
the internal ROM masks the external one after 4 cycles. That last bit is
important: the MCU still drives the address and data buses for internal ROM
reads. We'll get bus contention if both internal and external ROM drive the data
bus.

I used a `74'161A` 4 bit counter, a `74'74` flip-flop, a `74'00` quad NAND
and a `74'32` quad OR to implement the masking logic. There are probably dozens
of ways but this is what I had lying around.

The flip-flop input is tied high, its clock line to the counter output, and the
`RESET` line is tied to both counter & flip-flop clear. This makes the flip-flop
output go from 0 to 1 the first time after reset the counter outputs anything
and then it stays there until the next reset. The counter is clocked off of `E`
from the MCU. Call the output of the flip-flop `COUNT`.

To mask the ROM we can use the output enable (active low) on the ROM. Basically
take the existing `OEB` line and change it to `OEB OR (A12 AND COUNT)`. It's
active low so it will be inhibited if `A12 AND COUNT` returns true. That's never
the case in the first few cycles (`COUNT` is low) and after that it will only be
the case if `A12` is high. In our setup that's equivalent to an access to the
top 4k bytes.

NOTE: mode 0 is multiplexed. It needs a 74'373 or 74'573 on port 3 to separate
data and address lines. See the datasheet.

## Software setup

The trickiest part is to get what's likely going to be a non-standard baud rate
working. See the parent folders' READMEs for how to do that.

`serial_romdump.s` contains the dumping code. It's meant to go onto an 8k
ROM. After reset it waits for one byte to appear on serial, then writes the top
8k of the address space out on the serial line. If all went well that would be
the low 4k of the external ROM followed by the 4k internal ROM.

There is a `Makefile` to compile it with
[AS](http://john.ccac.rwth-aachen.de:8000/as/).

The low 4k of what's returned should start the string 'ROM dump OK' as a sanity
check.

`serial_romdump.py` implements the dumping. It assumes the serial converter is
FT232-based and uses some Linux-specific trickery to make things work with the
non-standard baud rate of the MCU. YMMV on other systems.

## Internal ROM contents & disassembly

`dumped_internal_rom.bin` contains the ROM as read from the chip.

You can disassemble it with `make dis` which assumes you have
[dasmfw](https://github.com/Arakula/dasmfw) installed.

There is a very partial `dumped_internal_rom.info` file that helps with making
sense of the first handful of instructions run after reset.

### What's in the ROM?

The startup code uses the DDR on all 4 ports, so we can assume that the MCU is
meant to run in single chip mode (mode 7).

The startup code runs a RAM test by writing to each location and reading it
back. Should that fail it lights up one line on P3. This suggests some
high-reliability application that somehow communicates a failing chip.

There are no obvious ASCII strings in the ROM so it doesn't look like it might
be driving some sort of user interface directly.
