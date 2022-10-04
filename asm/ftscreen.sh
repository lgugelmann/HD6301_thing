#!/bin/bash

# Linux is weird. The way you configure non-standard baud rates is to
# reconfigure what '38400' stands for. FT232 devices have a 24MHz oscillator on
# board that you can configure the 'divisor' for to reach your target baud
# rate. That's how you set up odd baud rates and what the setserial below does.

ROOT_FREQ=24000000
TARGET_BAUD=15625 # 1 MHz Crystal, E=250Khz, divisor 16
DIVISOR=$(($ROOT_FREQ/$TARGET_BAUD))
PORT=/dev/ttyUSB0

setserial $PORT spd_cust divisor $DIVISOR && screen $PORT 38400
