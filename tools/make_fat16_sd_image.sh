#!/bin/bash

set -e

# This script creates a 16MB FAT16 image file that can be used for SD card
# emulation.

if [ -z "$1" ]; then
    echo "Usage: $0 <output file>"
    exit 1
fi

# mkfs.vfat is often in /sbin or /usr/sbin, which is generally not in
# non-superuser $PATH.
PATH=$PATH:/sbin:/usr/sbin

# Using mkfs.fat as it stops if the parameters don't allow for a valid FAT16.
# mformat would just pick FAT12 which is not our target. 8170 is the smallest
# number of sectors for mkfs.fat to create a FAT16 filesystem.
mkfs.fat -F 16 -n "EIGHTBIT" -C "$1" 8208

echo "This is a test" | mcopy -i "$1" - ::TEST.TXT
echo "This is another test" | mcopy -i "$1" - ::FILETEST.BIN
mmd -i "$1" ::TEST_DIR
echo "This is a test in a directory named dir" | mcopy -i "$1" - ::TEST_DIR/DIR_TEST.txt
seq -s '' -w 1 1000 | mcopy -i "$1" - ::4001.TXT
seq -s '' -w 1 1000 | head -c 512 | mcopy -i "$1" - ::512.TXT
