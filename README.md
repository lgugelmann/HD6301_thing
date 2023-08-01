# Exploring the HD6301V1

I recently received a few samples of a Hitachi `HD6301V1` in a DIP-40
package. It's a 1MHz 8bit CPU derived from the Motorola 6801 with a few extra
instructions. It has an onboard mask-programmed 4k ROM, 128 bytes RAM, and 4 I/O
ports that can be used as some combination of data or address bus, or I/O
lines. It also comes with an onboard serial interface. Overall it's a really
neat chip!

- The datasheet for the HD6301v1 can be found
e.g. [here](https://pdf1.alldatasheet.com/datasheet-pdf/view/87402/HITACHI/HD6301V1.html).
- An _extensive_ handbook for the entire Hitachi 6301/6303 series of CPUs,
including copies of all datasheets, can be found on
[archive.org](https://archive.org/details/bitsavers_hitachidateriesHandbook1989_54281821).

This repository contains schematics and code to build a retro-inspired computer
around this chip. The idea is to build this off of modular components and a
shared bus so we can start small and build up piece by piece.

Currently I have a working device with Arduino-like ease of programmability, PS2
keyboard input, HDMI video output. It's complete enough for a working 'Snake'
clone that can be played over both serial console and video out.

## Getting started on a breadboard

The architecture is quite simple and similar to other 8-bit CPUs like
the 6502. There is one 16-bit address bus, one 8-bit data bus, a clock line, a
read/write line, and an interrupt line.

One way in which this chip departs from e.g. a 6502 is that it has several
different 'modes' that affect the memory layout and external buses. Mode
selection is done at startup using the first 3 lines of port 2. After startup
these lines can be reused for other purposes if desired. Mode 1
(non-multiplexed) breaks out all 16 address and 8 data lines. Mode 5 reduces the
external address bus to 8 lines. Mode 0/2/4/6 multiplex the data bus: at the
beginning of the cycle the 8 low address bits are presented, then an address
latch signal fires, then the 8 data bits are set. This frees up one 8-bit port
for I/O. Mode 7 has no external buses at all. Mode 0/5/6/7 map the internal ROM
to the top of the address space. It's inaccessible in 1/2/4. Mode 0 ('test
mode') has a special handling for the first read of the startup vector addresses
at $FFFE/$FFFF: instead of reading them from ROM (as it would in any subsequent
read) they are read over the external bus. This allows one to override the
ROM-interal startup vector while still keeping it accessible otherwise.

By selecting mode 1 at startup it's easy to get this chip to work on a
breadboard by just adding some external parallel RAM / ROM with a bit of address
decoding to select which one to access.

The chip has an internal 4x clock-divider. To get it to run at the max 1MHz you
need a 4MHz oscillator. I had no stability issues on a breadboard even at max
frequency.

The chip requires a minimum 100kHz to run (400kHz oscillator). Going below makes
it unstable.

## Random hardware notes

This is a collection of random notes about the hardware that are either
well-hidden in the datasheet or not documented at all.

- The IRQ line is not specified to be open-drain. It's likely not.
- The reset and standby pins are drawn with Schmitt-trigger inputs in the
  datasheet.
- The datasheet recommends 74'373 chips to latch the address bits in multiplexed
  mode. The '573 is probably a better choice for layout reasons as it has the
  same functionality but instead of having in/out pins next to each other it has
  all 8 inputs on one side and the 8 outputs on the other.
- P21 is not a general purpose I/O pin (even after mode selection). It is either
  a general input, or timer output.
- The mode selection / P2 multiplexing circuit in the datasheet would probably
  not work with a modern '4053 chip. The one in the datasheet has minimum
  switching times higher than the required hold times on the mode selection
  pins. Modern '4053 are faster.
- Reset and power-up are a bit finicky. The datasheet says reset must be held
  low for at least 20ms at power-on time. That's good advice. Without the wait
  time the MCU seems to start working a hair before the power rail reaches
  3V. The clock and data / address lines have their 'high' bits at that voltage
  too. At that point in time a typical 5V ROM might not even have powered up yet
  and you get all sorts of fun non-deterministic behavior. Something like a
  Diodes Inc. `APX811-46` is a great way to satisfy that requirement as it keeps
  reset low after power-up for 100-200ms.  Alternatively, the datasheet seems to
  suggest that the reset line has a Schmitt-trigger input. That would make an RC
  circuit with a long time constant also workable. Have not tried that.
- Exiting the standby mode requires holding reset low 20ms longer than STBY.

## The mask-programmed ROM

The samples I have come mask-programmed with some unknown code. The
`internal_rom` folder contains a dump of that ROM, some very preliminary
disassembly, and details on how to do such a dump using the 'test mode' of the
chip.

The ROM configures the data direction registers of all 4 I/O ports to a
combination of inputs/outputs. This suggests that it was meant to work in
single-chip mode. The MC6801 was popular in automotive applications - maybe this
one was meant to go into a car too.

## Contents of the repository

### Hardware modules

On the hardware side the repository contains
- The main CPU board. This can be used as a fully functional standalone
  single-board computer with 32kB RAM and ROM.
- A programming board to make programming the main CPU board as easy as
  programming an Arduino. This is a Raspberry Pi Pico-based board for USB
  connectivity to a host computer. It can put the main CPU into standby, take
  over the bus, and read or write to RAM/ROM as needed.
- A "video chip" board with HDMI output. This is another Pi Pico-based board
  that implements various graphical modes like a 100x40 or 100x75 text terminal
  with 6-bit color for each foreground and background. On the HDMI side it's
  800x600 pixel. On the bus it presents 64 write-only registers that implement
  various commands like setting characters, colors, or doing cursor moves.

Currently in progress are:
- An audio board based around the OPL3 FM synthesis chips.

### Software

#### HD6301 programs

The 'asm' folder contains software to run on the HD6301. There is a monitor
program for debugging, running other programs, and to provide some basic
libraries for PS2 keyboard handling and more.

There are also various test programs as well as a 'Snake' clone and a simple
audio sequencer.

See the docs in the 'asm' folder for more details.

#### Supporting software

- Much of the glue logic (address decoding, PS2 serial/parallel decoding, etc.)
  is implemented with GreenPAK SLG46826 programmable logic chips (see
  below). The 'greenpak' folder contains a simple python script to program these
  chips via i2c using an FTDI232h breakout board.
- The 'pico\_programmer' and 'pico\_graphics' folders contain the respective
  software for those those chips. The pico programmer has two parts: one to go
  onto the Pi Pico and a Python program to interface with it via serial port to
  do the actual programming.

## Bus documentation

All modules communicate over the following shared bus.

| Pin number | Function            | Notes                                                                        |
|------------|---------------------|------------------------------------------------------------------------------|
| 1..4       | Data bits D0..D3    | Bus users need to coordinate who's driving                                   |
| 5          | 5V                  | Only one device is allowed to power this                                     |
| 6          | GND                 |                                                                              |
| 7..10      | Data bits D4..D7    | Bus users need to coordinate who's driving                                   |
| 11..14     | Address bits A0..A3 | Bus users need to coordinate who's driving                                   |
| 15         | 3.3V                | Only one device is allowed to power this, optional                           |
| 16         | GND                 |                                                                              |
| 17..24     | A4..A11             | Bus users need to coordinate who's driving                                   |
| 25         | GND                 |                                                                              |
| 26         | 3.3V                | Only one device is allowed to power this, optional                           |
| 27..30     | A12..A15            | Bus users need to coordinate who's driving                                   |
| 31         | RX                  | Serial receive as seen from a device on the bus                              |
| 32         | TX                  | Serial transmit as seen from a device on the bus                             |
| 33         | CLK                 | Bus clock                                                                    |
| 34         | R/WB                | Read is high                                                                 |
| 35         | GND                 |                                                                              |
| 36         | 5V                  | Only one device is allowed to power this                                     |
| 37         | C1                  | Control line 1, up to the exact devices on the bus to decide what this does. |
| 38         | C2                  | Control line 2, up to the exact devices on the bus to decide what this does. |
| 39         | RESETB              | Active low reset. Open drain. Pullup on CPU board.                           |
| 40         | IRQB                | Active low interrupt. Open drain. Pullup on CPU board.                       |

This is 40 pins as it's easy to find 40pin IDC connectors and ribbon cables. The
power / GND pins are mirrored so that an upside-down connector has them in the
same place.

There are no chip select pins. Additional boards that want to fit into specific
parts of the memory map need to decode the address pins on their board and
coordinate with everyone else. Programmable GreenPAKs make this easy enough.

Pins are all push-pull unless stated otherwise. Any shared use needs to be
coordinated.

Logic levels are assumed 5V CMOS.

This is not how one designs for signal integrity - but it works well enough for
me. The user-friendliness of getting a ribbon cable to a breadboard to prototype
the next module is the overriding concern here.

### Bus details for HD6301 board

| Pin number | Function | Notes                                                                            |
|------------|----------|----------------------------------------------------------------------------------|
| 38         | STBYB    | Active low standby signal. For Pico Programmer to make HD6301 let go of the bus. |
