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

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Opl3 player')
    parser.add_argument('--port', default='/dev/ttyACM0')
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

    arguments = parser.parse_args()
    if not hasattr(arguments, 'func'):
        parser.print_help()
        sys.exit(1)

    try:
        arguments.func(arguments)
    except KeyboardInterrupt:
        pass
