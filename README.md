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

## In-circuit programming via USB

Beyond getting the HD6301 up and running another goal is to have proper
in-circuit programming via USB to make development as easy as possible.

It would be nice to get to fully self-hosted development, but on the road to get
there, using a modern PC is _much_ nicer.

I have a ZIF socket on my 6502 SBC. Nice enough, but the cycle of ROM out, ROM
into programmer, program ROM, ROM back into ZIF is tedious.

Here the goal is to get away from needing a ZIF socket or having to pre-program
any ROMs. As a bonus this makes it possible to use non-DIP packages for the ROM
which are a) much cheaper, and b) actually available at the moment.

### FT231x solution

At first I explored using an FTDI FT231x. It's 5V compatible and conveniently
also gets us USB serial. It has an 8-pin bitbang mode and 4 additional 'CBUS'
lines. Those can be programmed to be GPIOs, write strobes, and more.

The first solution was to use the 8bit GPIOs for data and CBUS for control
lines. Unfortunately the CBUS GPIO mode can't be used concurrently with the
bitbang mode and switching between bitbang and CBUS takes a _long_ time (think
hundreds of ms). Switching between the two for every byte makes things
unworkably slow.

Next up I turned to putting glue logic on a GreenPAK chip and using the
write-strobe mode. This works really nicely: first write puts the chip in
standby, subsequent writes latch data, address low, address high onto '74xx573
latches, and after that the GreenPAK automatically strobes clock & R/W. After a
few hundred ms without activity everything resets and the 6301 wakes back up.

This works really nicelay for write-only in-circuit programmer and there is a
tested working python-based programmer, and greenpak glue logic in this repo. I
didn't push for performance but got to ~15 seconds to write a 32K ROM.

Next up I tried to figure out how to implement a read-mode as well. In theory it
should be easy: put a 74'245 on the data bus, do the same latching trick as
before, but instead at the end bring OE low on the '245 and read the FT231x I/O
pins. A CBUS line can be used to put the GreenPAK into a read-mode with
different strobes / timings, but because that needs to be done only once at the
start it should be fast enough.

I got this to work - but it's sloooow because of USB latency, se below for
why. Tl;dr: switching between reading / writing on the FT231x for every byte
takes >1ms each time. This means 1 minute to read the whole address space at a
minimum. The fastest I could manage is ~3ms per byte before I gave up.

There could be possible solutions that do not require switching between reading
and writing at each byte. One option is to use a CBUS line for a read strobe and
discrete logic to increment an address counter at each strobe. You could set up
the counter at the start, then put the chip into read mode, and stream the data
back. This starts to get a bit silly and would also require futzing around with
the FT231x read buffers which seem to be particularly tricky to synchronize to
anything.

Another option is to use a synchronous bit-bang mode where a read is done on
every write and streamed back. The catch here is that you have to decide ahead
of time which pins are inputs and which are outputs. I could split the 8 bits
into 4 in / 4 out but that would also require extra glue logic that I didn't
feel like building.

### USB latency ruins the party

USB communication (before 3.0) is _always_ host-initiated. No usb device can
just decide to send data. It needs to be asked by the host whether there is
anything that it might want to send first. The fastest polling rate available
from the host side is 1ms.

This means that if switching between reading and writing requires a control
packet there is a mandatory 1ms latency hit: 1 packet with the writes (which can
be all sent bunched up into one), then a second packet with the command to
switch from read to write, then another packet to poll for the read data. On the
ft231x that's 2ms latency / byte at a minimum as we need to wait out the polling
interval twice.

### Pi Pico programmer

To avoid USB latency I need something capable of receiving a "read x bytes
starting at address y" command and then stream the data back. I have a spare Pi
Pico laying around so I went with that. It has enough I/O, has support for
multiple UARTs (so one for programming, one for comms with the HD6301), and
comes with both 5V and 3.3V rail which can be convenient. Downside: not 5V
tolerant.

The 5V issue is easy to solve: for the data output from the pico I use
TTL-compatible '74xxT573 latches, for the data input to the pico 3.3V '74LVHC245
buffers which are 5V tolerant. For the control lines I make use of the dual
supply feature of the _really really cool_ SLG46826 GreenPAK to have 3.3V I/O on
the pico side and tri-state 5V I/O on the HD6301 bus side.

### Internal ROM

The samples I have come mask-programmed with some mystery code. The
`internal_rom` folder contains a ROM dump and details on how I got it.

## Software

The asm folder contains some demo programs to get started.

### Assembler

It seems that the [AS](http://john.ccac.rwth-aachen.de:8000/as/) is a popular
choice that also supports the 4 extra instructions that the HD6301 introduced.
