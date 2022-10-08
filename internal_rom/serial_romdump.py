#!/usr/bin/python3

import os
import serial

# This assumes an FT232 based USB serial converter. If you have something else
# you need to figure out how to make it deal with nonstandard baud
# rates. Alternatively use a crystal that's at the right frequency to get a
# standard rate.

ROOT_FREQ=24000000
TARGET_BAUD=15625 # 1 MHz Crystal, E=250Khz, baud rate divisor 16
DIVISOR=ROOT_FREQ / TARGET_BAUD
PORT='/dev/ttyUSB0'
ROM_TEST_STRING=b'ROM dump OK'
ROM_DUMP_FILE='dumped_internal_rom.bin'

if __name__ == '__main__':
    # This changes the '38400' rate to actually be TARGET_BAUD
    os.system('setserial %s spd_cust divisor %s' % (PORT, DIVISOR))

    print("Dumping ROM...")

    ser = serial.Serial('/dev/ttyUSB0', 38400);
    # The dumping code is waiting to receive a byte as a signal to get started
    ser.write(b'x');
    s = ser.read(8192);

    f = open(ROM_DUMP_FILE, 'wb')
    if s.startswith(ROM_TEST_STRING):
        f.write(s[4096:]);
        print("Success!")
    else:
        print("Failed! Could not find the expected test string.")
        print("Writing out what we got.")

    f.close()
