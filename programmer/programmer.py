#!/usr/bin/python3

import argparse
import sys
import time
from pyftdi.ftdi import Ftdi
from pyftdi.gpio import GpioAsyncController

class Programmer:
    SECTOR_SIZE = 4096
    CBUS_PORT_MASK = 0x07
    CLK = 0x04

    COMMAND_STBY_ON  = 0
    COMMAND_STBY_OFF = 1
    COMMAND_RESET  = 3

    MODE_UNSET = 0
    MODE_GPIO = 1
    MODE_CBUS = 2

    def __init__(self, ftdi_url):
        self.ftdi_url = ftdi_url
        self.ftdi = Ftdi()
        self.gpio = GpioAsyncController()
        self.mode = self.MODE_UNSET

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self._close_with_latency_workaround(self.ftdi)
        self._close_with_latency_workaround(self.gpio.ftdi)

    def _close_with_latency_workaround(self, ftdi):
        if not ftdi.is_connected:
            return
        # This is a workaround for a bug in pyftdi. It sets the latency to 255
        # on close which messes up serial latency. The two lines below do what
        # freeze=False would do, except keeping the latency at 16.
        ftdi.set_bitmode(0, Ftdi.BitMode.RESET)
        ftdi.set_latency_timer(16)
        ftdi.close(freeze=True)

    def _set_mode(self, mode):
        if mode == self.mode:
            return

        self.mode = mode

        if mode == self.MODE_GPIO:
            self._close_with_latency_workaround(self.ftdi)
            self.gpio.configure(self.ftdi_url, direction=0xff)

        if mode == self.MODE_CBUS:
            self._close_with_latency_workaround(self.gpio.ftdi)
            self.ftdi.open_from_url(self.ftdi_url)
            self.ftdi.set_cbus_direction(self.CBUS_PORT_MASK, self.CBUS_PORT_MASK)

    def _write_byte(self, address, data):
        self._set_mode(self.MODE_GPIO)
        # latch low order bytes
        self.gpio.write(address % 256)
        # latch high order bytes
        self.gpio.write(address // 256)
        # latch data bytes
        self.gpio.write(data)
        # write strobe is generated by GreenPAK

    def _flash_write_byte(self, address, data):
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(0x5555 | 0x8000, 0xa0)
        self._write_byte(address | 0x8000, data)
        time.sleep(0.00002)

    def _flash_sector_erase(self, address):
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(0x5555 | 0x8000, 0x80)
        self._write_byte(0x5555 | 0x8000, 0xaa)
        self._write_byte(0x2aaa | 0x8000, 0x55)
        self._write_byte(address | 0x8000, 0x30)
        time.sleep(0.025)

    def send_cbus_command(self, command):
        self._set_mode(self.MODE_CBUS)
        self.ftdi.set_cbus_gpio(command)
        self.ftdi.set_cbus_gpio(command | self.CLK)
        self.ftdi.set_cbus_gpio(command)

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


def write_flash_command(args):
    with open(args.file, "rb") as input_file:
        data = input_file.read()

    base_address = args.address
    if base_address + len(data) -1 > 0xffff:
        print("Error: address + data size beyond 65k")
        sys.exit(1)

    if base_address % Programmer.SECTOR_SIZE:
        print("Error: write address not at sector boundary")
        sys.exit(2)

    with Programmer(args.ftdi_url) as prog:
        prog.flash_write_data(base_address, data)

def write_ram_command(args):
    with open(args.file, "rb") as input_file:
        data = input_file.read()[:args.size]

    base_address = args.address
    if base_address + len(data) -1 > 0xffff:
        print("Error: address + data size beyond 65k")
        sys.exit(1)

    with Programmer(args.ftdi_url) as prog:
        prog.ram_write_data(base_address, data)

def write_byte_command(args):
    with Programmer(args.ftdi_url) as prog:
        prog.ram_write_data(args.address, [args.byte])

def send_command_command(args):
    command = args.command
    if command < 0 or command > 5:
        print(f"Command {command} out of range")
        sys.exit(1)

    with Programmer(args.ftdi_url) as prog:
        prog.send_cbus_command(command)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='HD6301V1 in-circuit programmer')
    parser.add_argument('--ftdi_url', default='ftdi://ftdi:ft231x/1')
    subparsers = parser.add_subparsers()

    write_flash_parser = subparsers.add_parser('write_flash')
    write_flash_parser.add_argument('address', type=lambda x: int(x, 0))
    write_flash_parser.add_argument('file')
    write_flash_parser.add_argument('--size', type=lambda x: int(x,0),
                                    default=0x10000)
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

    arguments = parser.parse_args()
    if not hasattr(arguments, 'func'):
        parser.print_help()
        sys.exit(1)

    arguments.func(arguments)
