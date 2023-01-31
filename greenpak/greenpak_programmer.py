#!/usr/bin/python3

import argparse
import time
import sys
from pyftdi.i2c import I2cController

def hexdump(data):
    hex_data = [f'{byte:02x}' for byte in data]
    for i in range(0, 256, 16):
        print(' '.join(hex_data[i:i+16]))

def read_command(args):
    i2c = I2cController()
    i2c.configure(args.ftdi_url)

    i2c_address = args.i2c_address_base
    if args.location == 'nvm':
        i2c_address += 2
    if args.location == 'eeprom':
        i2c_address += 3

    port = i2c.get_port(i2c_address)

    mem_contents = port.exchange([0], 256)
    i2c.terminate()
    hexdump(mem_contents)

def write_command(args):
    i2c = I2cController()
    i2c.configure(args.ftdi_url)

    new_data = [0] * 256
    with open(args.file, encoding='utf-8') as config_file:
        for line in config_file:
            if line.startswith('index'):
                continue
            [index, value, _] = line.split('\x09\x09') # tab tab
            i = int(index)
            new_data[i // 8] += (value == '1') << (i % 8)
    print('Writing: ')
    hexdump(new_data)

    ram = i2c.get_port(args.i2c_address_base)

    i2c_address = args.i2c_address_base
    paged_write = False
    if args.location == 'nvm':
        i2c_address += 2
        paged_write = True
    if args.location == 'eeprom':
        i2c_address += 3
        paged_write = True

    if paged_write:
        # This avoids a bit of wear on the nvm / eeprom. We could do better and
        # mask the bits that aren't actually writable (mostly in page 15)
        port = i2c.get_port(i2c_address)
        old_data = list(port.exchange([0], 256))
        for i in range(0, 256, 16):
            if old_data[i:i+16] != new_data[i:i+16]:
                print(f'erasing page: {i // 16}')
                ram.write_to(0xe3, [0x80 + i // 16])
                # 20ms is the guaranteed maximum erase time
                time.sleep(0.02)
                port.write([i] + new_data[i:i+16])

        print('Written. Re-reading:')
        hexdump(port.exchange([0], 256))
    else:
        ram.write([0] + new_data)
        print('Written. Re-reading:')
        hexdump(ram.exchange([0], 256))

    i2c.terminate()

def reset_command(args):
    i2c = I2cController()
    i2c.configure(args.ftdi_url)
    ram = i2c.get_port(args.i2c_address_base)

    ram.write([200, 0x02])

    i2c.terminate()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=
                                     'FT232H-based GreenPAK programmer for i2c-programmable chips')
    parser.add_argument('--ftdi_url', default='ftdi://ftdi:232h/1')
    parser.add_argument('--i2c_address_base', type=lambda x: int(x,0), default=0x08)
    subparsers = parser.add_subparsers()

    read = subparsers.add_parser('read')
    read.add_argument('location', choices=['ram', 'nvm', 'eeprom'])
    read.set_defaults(func=read_command)

    write = subparsers.add_parser('write')
    write.add_argument('location', choices=['ram', 'nvm', 'eeprom'])
    write.add_argument('file', type=str)
    write.set_defaults(func=write_command)

    reset = subparsers.add_parser('reset')
    reset.set_defaults(func=reset_command)

    arguments = parser.parse_args()
    if not hasattr(arguments, 'func'):
        parser.print_help()
        sys.exit(1)

    arguments.func(arguments)
