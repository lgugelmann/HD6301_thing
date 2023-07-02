#!/usr/bin/python3

import argparse
import serial
import sys
import time

def write_command(args):
    serial_port = serial.Serial(args.port, 115200, timeout=0.5)

    if args.register < 0 or args.register > 0xF5:
        print("register out of range")
        exit(-1)

    if args.data < 0 or args.data > 0xff:
        print("data out of range")
        exit(-1)

    print(f"Writing data {args.data} to register {args.register}")
    serial_port.write(b'w')
    serial_port.write([args.register])
    serial_port.write([args.data])
    ret = serial_port.read(3)
    if ret[0] != ord('w'):
        print("Error writing!");
    else:
        print(f"Got {chr(ret[0])} reg {ret[1]} data {ret[2]}")

# https://www.vogons.org/viewtopic.php?t=55181
def test_opl3(args):
    serial_port = serial.Serial(args.port, 115200, timeout=0.5)

    def writeopl(register, data):
        serial_port.write(b'w')
        serial_port.write([register])
        serial_port.write([data])

    # reset
    for reg in range(0x20, 0xff):
        if (reg & 0xe0) == 0x80:
            writeopl(reg, 0x0f)
        else:
            writeopl(reg, 0)

    writeopl(0x20, 0x03)
    writeopl(0x23, 0x01)
    writeopl(0x40, 0x2f)
    writeopl(0x43, 0x00)
    writeopl(0x61, 0x10)
    writeopl(0x63, 0x10)
    writeopl(0x80, 0x00)
    writeopl(0x83, 0x00)
    writeopl(0xa0, 0x44)
    writeopl(0xb0, 0x12)
    writeopl(0xc0, 0xfe)

    writeopl(0xb0, 0x32)
    time.sleep(0.00001)
    writeopl(0x60, 0xf0)


def play_command(args):
    with open(args.file, 'rb') as vgm_file:
        vgm = vgm_file.read();

    serial_port = serial.Serial(args.port, 115200, timeout=0.5)

    # validate vgm header
    header = b'Vgm '
    if vgm[0:4] != header:
        print(f"Bad header: {vgm[0:4]} vs expected {header}")
        return

    freq = 44100

    offset = int.from_bytes(vgm[0x34:0x34+4], byteorder='little')
    if offset == 0:
        offset = 0x40
    else:
        offset = 0x34 + offset
    vgm_iter = iter(vgm[offset:])

    serial_port.write(b'x')

    serial_buffer = []
    while vgm_iter:
        command = next(vgm_iter)
        if command == 0x5e:
            # Write to array 0
            serial_buffer.append(ord('w'))
            serial_buffer.append(next(vgm_iter)) # register
            serial_buffer.append(next(vgm_iter)) # data
        elif command == 0x5f:
            # Write to array 1
            serial_buffer.append(ord('W'))
            serial_buffer.append(next(vgm_iter)) # register
            serial_buffer.append(next(vgm_iter)) # data
        elif command == 0x61:
            serial_port.write(serial_buffer)
            serial_buffer = []

            samples = next(vgm_iter) + (next(vgm_iter) << 8);
            time.sleep(samples / freq)
        elif command == 0x63:
            serial_port.write(serial_buffer)
            serial_buffer = []

            samples = 882
            time.sleep(samples / freq)
        elif command == 0x66:
            print('The End!')
            break
        else:
            print(f'unknown command: {command:02x}')
            break

class operator():
    def __init__(self):
        self.am = 0
        self.vib = 0
        self.egt = 1
        self.ksr = 0
        self.mult = 1

        self.ksl = 0
        self.tl = 0
        self.ar = 0
        self.dr = 0
        self.sl = 0
        self.rr = 0

    def get_data(self):
        byte0 = (self.am << 7) + (self.vib << 6) + (self.egt << 5) + (self.ksr << 4) + self.mult
        byte1 = (self.ksl << 6) + (63 - self.tl)
        byte2 = (self.ar << 4) + self.dr
        byte3 = (15 - self.sl << 4) + self.rr
        byte4 = self.ws

        return [byte0, byte1, byte2, byte3, byte4]


class instrument():
    def __init__(self):
        self.op1 = operator()
        self.op2 = operator()

        self.ch = 3
        self.fb = 0
        self.cnt = 0

    def get_data(self):
        byte0 = (self.ch << 4) + (self.fb << 1) + self.cnt


def piano_instrument():
    # Piano Instrument
    # AM VIB EGT KSR MULT
    piano = instrument()
    o = piano.op1
    o.am = 0
    o.vib = 0
    o.egt = 1
    o.ksr = 0
    o.mult = 1

    o.ksl = 2
    o.tl = 48
    o.ar = 15
    o.dr = 2
    o.sl = 4
    o.rr = 5

    o.ws = 0

    o = piano.op2
    o.am = 0
    o.vib = 0
    o.egt = 1
    o.ksr = 0
    o.mult = 1

    o.ksl = 0
    o.tl = 57
    o.ar = 15
    o.dr = 2
    o.sl = 7
    o.rr = 6

    o.ws = 0

    piano.ch = 3
    piano.fb = 4
    piano.cnt = 0

    return piano

def organ_instrument():
    # Organ Instrument
    # AM VIB EGT KSR MULT
    organ = instrument()
    o = organ.op1
    o.am = 0
    o.vib = 1
    o.egt = 1
    o.ksr = 0
    o.mult = 1

    o.ksl = 0
    o.tl = 44
    o.ar = 9
    o.dr = 7
    o.sl = 15
    o.rr = 4

    o.ws = 1

    o = organ.op2
    o.am = 1
    o.vib = 0
    o.egt = 1
    o.ksr = 1
    o.mult = 1

    o.ksl = 2
    o.tl = 63
    o.ar = 15
    o.dr = 5
    o.sl = 15
    o.rr = 4

    o.ws = 0

    organ.ch = 3
    organ.fb = 0
    organ.cnt = 0

    return organ

def midi_to_freq(midi):
    if midi < 21:
        raise Exception("midi note too low")

    freqs = {0 : [0x3F, 1],
             1 : [0x52, 1],
             2 : [0x67, 1],
             3 : [0x7c, 1],
             4 : [0x92, 1],
             5 : [0xaa, 1],
             6 : [0xc4, 1],
             7 : [0xdf, 1],
             8 : [0xfb, 1],
             9 : [0x19, 2],
             10: [0x39, 2],
             11: [0x5b, 2]}

    # Midi 69 is A4 = 440Hz
    freq = freqs[(midi - 9) % 12]
    block = (midi - 9) // 12

    return [freq[0], freq[1] + (block << 2)]


def instrument_test_command(args):
    serial_port = serial.Serial(args.port, 115200, timeout=0.5)

    def write0(register, data):
        serial_port.write(b'w')
        serial_port.write([register])
        serial_port.write([data])
        print(f'addr: {register:03x} data: {data:02x}')

    def write1(register, data):
        serial_port.write(b'w')
        serial_port.write([register])
        serial_port.write([data])

    def set_on_channel(instr, channel):
        for count, data in enumerate(instr.op1.get_data()):
            if count != 4:
                write0(0x20*count + 0x20 + channel-1, data)
            else:
                write0(0xe0 + channel-1, data)
        for count, data in enumerate(instr.op2.get_data()):
            if count != 4:
                write0(0x20*count + 0x23 + channel-1, data)
            else:
                write0(0xe3 + channel-1, data)

    def channel_on(channel, note):
        freq = midi_to_freq(note)
        write0(0xa0 + channel - 1, freq[0])
        write0(0xb0 + channel - 1, freq[1] + 0x20)

    def channel_off(channel):
        write0(0xb0 + channel - 1, 0)

    write1(0x05, 1) # Enable OPL3 features

    piano0 = piano_instrument()
    piano1 = piano_instrument()
    organ0 = organ_instrument()
    organ1 = organ_instrument()
    set_on_channel(piano0, 1)
    set_on_channel(piano1, 2)

    for note in range(69, 69+12, 2):
        print(f'note: {note}')
        channel_off(2)
        channel_on(1, note)
        time.sleep(0.3)
        channel_off(1)
        print(f'note: {note+1}')
        channel_on(2, note+1)
        time.sleep(0.3)

    channel_off(1)
    channel_off(2)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Opl3 player')
    parser.add_argument('--port', default='/dev/ttyACM1')
    subparsers = parser.add_subparsers()

    write_parser = subparsers.add_parser('write')
    write_parser.add_argument('register', type=lambda x: int(x, 0))
    write_parser.add_argument('data', type=lambda x: int(x, 0))
    write_parser.set_defaults(func=write_command)

    play_parser = subparsers.add_parser('play')
    play_parser.add_argument('file')
    play_parser.set_defaults(func=play_command)

    test_parser = subparsers.add_parser('test')
    test_parser.set_defaults(func=test_opl3)

    instrument_parser = subparsers.add_parser('instrument')
    instrument_parser.set_defaults(func=instrument_test_command)

    arguments = parser.parse_args()
    if not hasattr(arguments, 'func'):
        parser.print_help()
        sys.exit(1)

    try:
        arguments.func(arguments)
    except KeyboardInterrupt:
        pass
