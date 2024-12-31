#!/usr/bin/env python3

'''A utility to print basic information about a FAT16 image file.'''

import sys

class Fat16():
    '''Parse a FAT16 image file and print basic information about it.'''
    def __init__(self, image):
        self.image = image
        self.boot_sector = image[0:512]
        self.oem_name = self.boot_sector[0x03:0x03+8].decode("utf-8")
        self.bytes_per_sector = int.from_bytes(self.boot_sector[0x0b:0xb+2], byteorder="little")
        self.sectors_per_cluster = int.from_bytes(self.boot_sector[0x0d:0x0d+1], byteorder="little")
        self.reserved_sectors = int.from_bytes(self.boot_sector[0x0e:0x0e+2], byteorder="little")
        self.number_of_fats = int.from_bytes(self.boot_sector[0x10:0x10+1], byteorder="little")
        self.maximum_number_of_root_directory_entries = int.from_bytes(
            self.boot_sector[0x11:0x11+2], byteorder="little")
        self.total_sectors = int.from_bytes(self.boot_sector[0x13:0x13+2], byteorder="little")
        self.media_descriptor = int.from_bytes(self.boot_sector[0x15:0x15+1], byteorder="little")
        self.sectors_per_fat = int.from_bytes(self.boot_sector[0x16:0x16+2], byteorder="little")
        self.sectors_per_track = int.from_bytes(self.boot_sector[0x18:0x18+2], byteorder="little")
        self.number_of_heads = int.from_bytes(self.boot_sector[0x1a:0x1a+2], byteorder="little")
        self.hidden_sectors = int.from_bytes(self.boot_sector[0x1c:0x1c+4], byteorder="little")
        self.logical_drive_number = int.from_bytes(self.boot_sector[0x24:0x24+1],
                                                   byteorder="little")
        self.extended_signature = int.from_bytes(self.boot_sector[0x26:0x26+1], byteorder="little")
        self.volume_serial_number = int.from_bytes(self.boot_sector[0x27:0x27+4],
                                                   byteorder="little")
        self.volume_label = self.boot_sector[0x2b:0x2b+11].decode("utf-8")
        self.file_system_type = self.boot_sector[0x36:0x36+8].decode("utf-8")

        self.fat_start = self.reserved_sectors * self.bytes_per_sector
        self.fat_size = self.sectors_per_fat * self.bytes_per_sector
        self.fat = image[self.fat_start:self.fat_start + self.fat_size]
        self.root_directory_start = self.fat_start + self.number_of_fats * self.fat_size
        self.root_directory_size = self.maximum_number_of_root_directory_entries * 32
        self.root_directory = image[self.root_directory_start:self.root_directory_start
                                     + self.root_directory_size]
        self.data_start = self.root_directory_start + self.root_directory_size
        self.data_start_sector = self.data_start // self.bytes_per_sector
        self.root_directory_start_sector = self.root_directory_start // self.bytes_per_sector

    def print_boot_sector_info(self):
        print('Boot sector information:')
        print(f' OEM name: {self.oem_name}')
        print(f' Bytes per sector: {self.bytes_per_sector}')
        print(f' Sectors per cluster: {self.sectors_per_cluster}')
        print(f' Reserved sectors: {self.reserved_sectors}')
        print(f' Number of FATs: {self.number_of_fats}')
        print(f' Maximum number of root directory entries: '
              f'{self.maximum_number_of_root_directory_entries}')
        print(f' Total sectors: {self.total_sectors}')
        print(f' Media descriptor: {self.media_descriptor}')
        print(f' Sectors per FAT: {self.sectors_per_fat}')
        print(f' Sectors per track: {self.sectors_per_track}')
        print(f' Number of heads: {self.number_of_heads}')
        print(f' Hidden sectors: {self.hidden_sectors}')
        print(f' Logical drive number: {self.logical_drive_number}')
        print(f' Extended signature: {self.extended_signature}')
        print(f' Volume serial number: {self.volume_serial_number}')
        print(f' Volume label: {self.volume_label}')
        print(f' File system type: {self.file_system_type}')
        print()

    def print_fat_information(self):
        fat = self.fat
        visited = [False] * (len(fat) // 2)
        print('FAT information:')
        print(f' FAT size: {len(fat)} bytes / {len(fat) // 2} entries / '
              f'{len(fat) // self.bytes_per_sector} sectors')
        print(f' FAT ID: {int.from_bytes(fat[0:2], byteorder="little"):04x}')
        end_of_chain_marker = int.from_bytes(fat[2:4], byteorder="little")
        print(f' End-of-chain marker: {end_of_chain_marker:04x}')
        print(' Chains: (as cluster numbers)')
        for i in range(1, len(fat) // 2):
            if not visited[i]:
                if int.from_bytes(fat[2*i:2*i+2], byteorder='little') == 0:
                    visited[i] = True
                    continue
                print(f'    {i}: ', end='')
                visited[i] = True
                j = int.from_bytes(fat[2*i:2*i+2], byteorder='little')
                while j < 0xfff8:
                    print(f'{j} -> ', end='')
                    if visited[j]:
                        print('Cycle detected!')
                        break
                    visited[j] = True
                    j = int.from_bytes(fat[2*j:2*j+2], byteorder='little')
                print('EOF')
        print()

    def print_root_directory(self):
        directory = self.root_directory
        directory_clusters = []
        print('Root directory information:')
        print(f' Root directory start sector: {self.root_directory_start_sector}')
        print(f' Root directory max size: {len(directory)} bytes / '
              f'{self.maximum_number_of_root_directory_entries} entries')
        print(' Entries:')
        for i in range(0, len(directory), 32):
            if directory[i] == 0:
                # End marker - no further entries
                break
            if directory[i] == 0xe5:
                # entry deleted
                print(f'    {i // 32:04d}: [deleted entry]')
                continue
            attributes = int(directory[i+11])
            cluster = int.from_bytes(directory[i+26:i+28], byteorder="little")
            if cluster == 0:
                sector = self.root_directory_start_sector
            else:
                sector = self.data_start_sector + (cluster - 2) * self.sectors_per_cluster
            size = int.from_bytes(directory[i+28:i+32], byteorder="little")
            print(f'    {i // 32:04d}: {directory[i:i+8].decode("utf-8")} '
                  f'{directory[i+8:i+11].decode("utf-8")}, '
                  f'attributes: {attributes:02x}, cluster: {cluster} (sector: {sector}), '
                  f'size: {size} bytes')
            if int(directory[i+11]) & 0x10 != 0:
                directory_clusters.append(int.from_bytes(directory[i+26:i+28], byteorder="little"))
        return directory_clusters

    def print_directory_at_cluster(self, cluster):
        directory_start = (self.data_start + (cluster - 2) *
            self.bytes_per_sector * self.sectors_per_cluster)
        directory = self.image[directory_start:directory_start +
                               self.bytes_per_sector * self.sectors_per_cluster]
        directory_clusters = []
        print(f'Directory at cluster {cluster}:')
        print(f'    Start sector: {directory_start // self.bytes_per_sector}')
        print(f'    Size: {len(directory)} bytes')
        print('    Entries:')
        for i in range(0, len(directory), 32):
            if directory[i] == 0:
                # End marker - no further entries
                break
            if directory[i] == 0xe5:
                # entry deleted
                print(f'        {i // 32:04d}: [deleted entry]')
                continue
            attributes = int(directory[i+11])
            cluster = int.from_bytes(directory[i+26:i+28], byteorder="little")
            if cluster == 0:
                sector = self.root_directory_start_sector
            else:
                sector = self.data_start_sector + (cluster - 2) * self.sectors_per_cluster
            size = int.from_bytes(directory[i+28:i+32], byteorder="little")
            print(f'        {i // 32:04d}: {directory[i:i+8].decode("utf-8")} '
                  f'{directory[i+8:i+11].decode("utf-8")}, '
                  f'attributes: {attributes:02x}, cluster: {cluster} (sector: {sector}), '
                  f'size: {size} bytes')
            if attributes & 0x10 != 0 and directory[i] != 0x2e:
                directory_clusters.append(int.from_bytes(directory[i+26:i+28], byteorder="little"))
        return directory_clusters

def main():
    img = []
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} image")
        sys.exit(-1)
    with open(sys.argv[1], 'rb') as f:
        img = f.read()

    fat16 = Fat16(img)
    fat16.print_boot_sector_info()
    fat16.print_fat_information()
    dir_clusters = fat16.print_root_directory()
    while len(dir_clusters) > 0:
        cluster = dir_clusters.pop()
        print()
        dir_clusters += fat16.print_directory_at_cluster(cluster)

if __name__ == '__main__':
    main()