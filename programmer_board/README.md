# Pico programmer board

WARNING: the 1.0 version of the board is buggy. At a minimum it requires adding
pullups on all OE pins. The pico does not start quickly enough to put them into
a good state. The resulting bus contention uses enough power to render the pico
unable to start. For any sane current limit the '5V' rail gets drawn down to
less than the ~1.8V needed for the pico boost converter to start working.

The Schematic & PCB have not been updated to fix this bug yet.

It's not hard to fix the v1.0 board by reworking some extra resistors onto it.

## TODOs for the next revision

- Add pull-ups on all OE pins and D0..D7 to guarantee safe initial state
- Jumpers for all power rails to isolate the pico
