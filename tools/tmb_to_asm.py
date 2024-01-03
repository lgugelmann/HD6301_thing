#!/usr/bin/python3

"""Converts a TMB file (Apogee Sound System instrument format) to an assembly
file with instruments in the format understood by the asm/ code. Output is on
stdout.
"""

import sys

HEADER="""        ifndef __general_midi_instruments
__general_midi_instruments = 1

general_midi_instruments:"""

FOOTER="""
        endif"""

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} input_file.tmb")
        sys.exit(-1)
    with open(sys.argv[1], 'rb') as tmb_file:
        print(HEADER)
        for n in range(256):
            b = tmb_file.read(13)
            instr = [b[0], b[2], b[4], b[6], b[8],
                     b[1], b[3], b[5], b[7], b[9], b[10] + 0x30]
            print(f"        .byt {','.join([f'${n:02x}' for n in instr])}")
        print(FOOTER)
