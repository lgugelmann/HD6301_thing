# Exploring the HD6301V1

I recently received a Hitachi `HD6301V1` in a DIP-40 package to play around
with. It's an 8bit MPU that's a Motorola 6801 derivative with a few extra
instructions. It has an onboard mask-programmed 4k ROM, 128 bytes RAM, and 4 I/O
ports that can be used as some combination of data or address bus, or I/O lines.

It's a really neat chip and comes with an onboard serial interface too.

Datasheets can be found
e.g. [here](https://pdf1.alldatasheet.com/datasheet-pdf/view/87402/HITACHI/HD6301V1.html). Also
interesting is this _extensive_ handbook for the entire Hitachi 6301/6303 series
of MCUs found
[here](https://www.jaapsch.net/psion/pdffiles/hd6301-3_handbook.pdf).

## Hardware

### Getting the MCU up and running

You can find a schematic for how to add external RAM/ROM and get started with
this chip in the 'board' folder. It works nicely on a breadboard.

NOTE: I found reset to be a bit finicky. The datasheet says the reset line must
be held low for at least 20ms at power-on time. That's probably wise. Without
the wait time the MCU seems to start working a hair before the power rail
reaches 3V. You'll get `E` and the data / address lines at that voltage too,
trying to reach a 5V ROM that might not even have powered up yet.  I ended up
making a little reset circuit out of a button and a Diodes Inc. `APX811-46` that I
had lying around. That keeps reset low after power-up for 100-200ms.
Alternatively, the datasheet seems to suggest that the reset line has a
Schmitt-trigger input. That would make an RC circuit with a long time constant
also workable. Have not tried that.

### Internal ROM

The samples I have come mask-programmed with some mystery code. The
`internal_rom` folder contains ROM dumping instructions and a ROM dump.

## Software

The asm folder contains some demo programs to get started.

### Assembler

It seems that the [AS](http://john.ccac.rwth-aachen.de:8000/as/) is a popular
choice that also supports the 4 extra instructions that the HD6301 introduced.
