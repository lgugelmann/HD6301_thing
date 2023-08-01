# Pi Pico programmer board

WARNING: the 1.0 version of the board is buggy. At a minimum it requires adding
pullups on all OE pins. The Pico does not start quickly enough to put them into
a good state. The resulting bus contention uses enough power to render the Pico
unable to start. For any sane current limit the '5V' rail gets drawn down to
less than the ~1.8V needed for the Pico boost converter to start working. The
Schematic & PCB have not been updated to fix this bug yet. It's not hard to fix
the v1.0 board by reworking some extra resistors onto it.

An common way to program ROMs for 8-bit hobby projects are ZIF sockets. Take ROM
out, put it into programmer, program it, put it back into the ZIF socket,
run. That works, but it gets tedious _really fast_. It's also getting harder and
harder to find parallel ROMs in DIP packages at reasonable prices. The solution
is to bite the bullet and use soldered-down ROMs in smaller packages and figure
out in-circuit programming.

The goal here is to make programming the HD6301 board as easy as programming an
Arduino from a modern computer, while at the same time keeping the HD6301 board
able to work independently without this development companion.

This is accomplished by having a Pi Pico expose a serial console to receive
commands from a computer. To program the HD6301 board the Pico puts the CPU into
standby mode, takes over the bus, reads or writes to arbitrary memory locations
(which allows it to program the ROM among other things), and wakes the HD6301
back up.

This technique makes for a fairly generic interface to many 8-bit era
devices. It could work easily for a 6502-based computer as well (at least for
versions with a bus-enable line).

Programming 8k (2 sectors) of ROM takes just a few seconds.

The board itself is pretty simple. The main issue is that the Pico does not have
enough I/O lines, and that it's not 5V tolerant. This means we need both
multiplexing and voltage translation. The address and data pins are all
connected in groups of 8 pins to a pair of 74LVC245 bus buffers and 74HCT573
latches. The latch holds the output data, while the '245 does voltage
translation for inputs. It's up to the software on the Pico to correctly juggle
the various output enable pins to avoid bus contention. The remaining lines are
voltage-translated by a GreenPAK programmable logic chip that can do 3.3V on
some GPIOs and 5V, tri-state on others.

The Pico was chosen here because it's trivial to prototype with it and it makes
it easy to expose 2 serial lines on the USB side. One is used for programming,
and one to communicate with the HD6301's own serial interface.

The initial plan was to use a much dumber chip like FTDI USB interfaces and put
all the logic on the other side of the serial interface. USB latency(!) made
that plan unworkable. See below for the details.

## TODOs for the next revision

- Add pull-ups on all OE pins and D0..D7 to guarantee safe initial state
- Jumpers for all power rails to isolate the Pico
- Some solution for power rail filtering. My desktop's USB ports are
  _NOISY_. Not a problem for digital work - but the OPL3 audio board suffers
  from bad filtering here.

## In-circuit programming via USB - the gory details

## FT231x solution

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

This works really nicely for a write-only in-circuit programmer. There is a
tested working python-based programmer and the necessary GreenPAK glue logic in
this repo. I didn't push for performance but got to ~15 seconds to write a 32K
ROM.

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

The 5V issue is easy to solve: for the data output from the Pico I use
TTL-compatible '74xxT573 latches, for the data input to the Pico 3.3V '74LVHC245
buffers which are 5V tolerant. For the control lines I make use of the dual
supply feature of the _really really cool_ SLG46826 GreenPAK to have 3.3V I/O on
the Pico side and tri-state 5V I/O on the HD6301 bus side.
