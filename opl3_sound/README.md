# OPL3 sound board

## Hardware

This board contains OPL3 chips: YMF262 (the digital FM sound chip) and its
compainon YAC512 digital-analog-converter. It also has a mono amplifier for a
small speaker and a stereo one for headphones. Plugging headphones in/out
switches from spaker to headphones or back.

### Quirks and possible gotchas

- Writes are triggered on the *rising* edge of WR/CS (whichever is first)

#### Timer interrupts

The OPL3 datasheet omits to mention that when writing a 1 to the `RST` bit in
register `$04`, all other bits are ignored.

This is convenient as it allows IRQ routines to clear the interrupt without
needing to keep track of the rest of the register.

#### Hardware notes / bugs in v2

- Volume doesn't go all the way down
- Forgot pullups for i2c on GreenPAK

## Software

There is a small utility `opl3.py` to interface with the OPL3 board via serial
port. This requires the `serial_opl3` program to be running on the 6301 thing.

The most interesting functionality is probably `opl3.py play [vgm file]` which
plays a VGM file intended for OPL3 on the sound board.

The rest of the functionality is probably in various states of disrepair. It was
used for board bring-up and early testing and not maintained since.
