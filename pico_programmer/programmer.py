#!/usr/bin/python3

import argparse
import serial
import sys
import time

class Programmer:
    SECTOR_SIZE = 4096

    def __init__(self, serial_port):
        self.port = serial_port
        self.standby = False
        self.data = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        # accumulating all data and writing it in one go is a massive speedup as
        # it lets us be maximally efficient with USB packets to the device.
        if len(self.data) > 0:
            self.port.write(self.data)
        self.exit_stby()

    def _write_byte(self, address, data):
        if self.standby == False:
            raise Exception("Attempting write without being in standby")
        self.port.write([
            # latch low address byte
            ord('l'), address % 256,
            # latch high address byte
            ord('h'), address // 256,
            # latch data byte
            ord('d'), data,
            ord('w')
        ])

    def _flash_write_byte(self, address, data):
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(0x5555 | 0x8000, 0xa0)
        self._write_byte(address | 0x8000, data)

    def _flash_sector_erase(self, address):
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(0x5555 | 0x8000, 0x80)
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(address | 0x8000, 0x30)
        time.sleep(0.025)

    def flash_write_data(self, start_address, data):
        for start in range(0, len(data), self.SECTOR_SIZE):
            end = min(start+self.SECTOR_SIZE, len(data))
            address = start_address + start

            print(f"Erasing sector at {address:04x}")
            self._flash_sector_erase(address)

            print(f"Writing bytes {start} to {end-1} at {address:04x}")
            for i in range(start, end):
                self._flash_write_byte(start_address + i, data[i])

    def ram_write_data(self, start_address, data):
        for index, byte in enumerate(data):
            self._write_byte(start_address + index, byte)

    def read_data(self, address, size):
        if address + size -1 > 0xffff:
            raise Exception(f'Out of bounds read: address {address:04x}, size {size:04x}')
        send = [
            ord('r'),
            address % 256, address // 256,
            (address+size-1) % 256, (address+size-1) // 256
        ]
        print(f'sending {send}')
        self.port.write(send)
        check = self.port.read(1)
        if check != b'r':
            raise Exception(f'Failed to set up read {check}')

        data = []
        for s in range(0, size // 1024):
            data += self.port.read((s+1)*1024)
            size -= 1024
        if size:
            data += self.port.read(size)

        extra = self.port.read(10)
        if extra:
            raise Exception(f'Got unexpected extra data {extra}')

        return data

    def enter_stby(self):
        self.port.write(b's')
        time.sleep(0.01)
        self.standby = True

    def exit_stby(self):
        self.port.write(b'x')
        self.standby = False

def write_flash_command(args):
    with open(args.file, "rb") as input_file:
        data = input_file.read()

    base_address = 0x10000 - len(data)
    if args.address:
        base_address = args.address

    if base_address + len(data) > 0x10000:
        print("Error: address + data size beyond 65k")
        sys.exit(1)

    if base_address % Programmer.SECTOR_SIZE:
        print("Error: write address not at sector boundary")
        sys.exit(2)

    serial_port = serial.Serial(args.port, 115200)

    with Programmer(serial_port) as prog:
        prog.enter_stby()
        prog.flash_write_data(base_address, data)

def write_ram_command(args):
    with open(args.file, "rb") as input_file:
        data = input_file.read()[:args.size]

    base_address = args.address
    if base_address + len(data) -1 > 0xffff:
        print("Error: address + data size beyond 65k")
        sys.exit(1)

    serial_port = serial.Serial(args.port, 115200, timeout=1)

    with Programmer(serial_port) as prog:
        prog.enter_stby()

        prog.ram_write_data(base_address, data)

def write_byte_command(args):
    serial_port = serial.Serial(args.port, 115200)

    with Programmer(serial_port) as prog:
        prog.enter_stby()
        prog.ram_write_data(args.address, [args.byte])

def read_command(args):
    serial_port = serial.Serial(args.port, 115200, timeout=0.5)

    with Programmer(serial_port) as prog:
        prog.enter_stby()
        # size 0 means 'read one byte'
        data = prog.read_data(args.address, args.size)

    if args.file == '-':
        print(data)
    else:
        with open(args.file, "wb") as output_file:
            output_file.write(bytes(data))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='HD6301V1 in-circuit programmer')
    parser.add_argument('--port', default='/dev/ttyACM0')
    subparsers = parser.add_subparsers()

    write_flash_parser = subparsers.add_parser('write_flash')
    write_flash_parser.add_argument('file')
    write_flash_parser.add_argument('--address', type=lambda x: int(x, 0))
    write_flash_parser.set_defaults(func=write_flash_command)

    write_ram_parser = subparsers.add_parser('write_ram')
    write_ram_parser.add_argument('address', type=lambda x: int(x, 0))
    write_ram_parser.add_argument('file')
    write_ram_parser.add_argument('--size', type=lambda x: int(x,0),
                                  default=0x10000)
    write_ram_parser.set_defaults(func=write_ram_command)

    write_byte_parser = subparsers.add_parser('write_byte')
    write_byte_parser.add_argument('address', type=lambda x: int(x, 0))
    write_byte_parser.add_argument('byte', type=lambda x: int(x, 0))
    write_byte_parser.set_defaults(func=write_byte_command)

    read_parser = subparsers.add_parser('read')
    read_parser.add_argument('address', type=lambda x: int(x, 0))
    read_parser.add_argument('--size', type=lambda x: int(x, 0), default=1)
    read_parser.add_argument('--file', default='-')
    read_parser.set_defaults(func=read_command)

    arguments = parser.parse_args()
    if not hasattr(arguments, 'func'):
        parser.print_help()
        sys.exit(1)

    arguments.func(arguments)
