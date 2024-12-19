#!/usr/bin/env python3
"""
Parse a MIDI file and print tempo information.

Usage:
    midi_tempo_stats.py <midi_file_path>

Example:
    python midi_tempo_stats.py example.mid
"""

import sys
import struct

def ticks_to_ms(ticks, tempo, ticks_per_beat):
    """Convert ticks to milliseconds using the given tempo and ticks per beat."""
    return (ticks * tempo) / (ticks_per_beat * 1000)

def read_varlen(data, index):
    """Read a MIDI standard variable-length quantity."""
    value = 0
    while True:
        byte = data[index]
        index += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            break
    return value, index

def parse_midi_file(midi_file_path):
    """Parse a MIDI file and print tempo information."""
    with open(midi_file_path, 'rb') as f:
        data = f.read()

    assert data[:4] == b'MThd', "Not a valid MIDI file"
    _, num_tracks, ticks_per_beat = struct.unpack(">HHH", data[8:14])
    print(f"Ticks per beat: {ticks_per_beat}")

    index = 14
    tempo = 500000  # Default tempo (microseconds per beat)
    total_length_ms = 0
    max_delta_ms = 0

    for _ in range(num_tracks):
        assert data[index:index+4] == b'MTrk', "Not a valid track"
        track_size = struct.unpack(">I", data[index+4:index+8])[0]
        index += 8
        track_end = index + track_size

        track_length_ms = 0
        while index < track_end:
            delta_ticks, index = read_varlen(data, index)
            event_type = data[index]
            index += 1

            if event_type == 0xFF:  # Meta event
                meta_type = data[index]
                index += 1
                meta_length, index = read_varlen(data, index)
                meta_data = data[index:index+meta_length]
                index += meta_length

                if meta_type == 0x51:  # Set tempo
                    tempo = struct.unpack(">I", b'\x00' + meta_data)[0]
                    bpm = 60000000 / tempo
                    print(f"Microseconds per tick: {tempo / ticks_per_beat}")
                    print(f"Tempo: {tempo} microseconds per beat")
                    print(f"Beats per minute (BPM): {bpm}")

            elif event_type & 0xF0 in (0x80, 0x90):  # Note on/off events
                index += 2  # Skip the note and velocity bytes
                time_delta_ms = ticks_to_ms(delta_ticks, tempo, ticks_per_beat)
                track_length_ms += time_delta_ms
                max_delta_ms = max(max_delta_ms, time_delta_ms)

        total_length_ms = max(total_length_ms, track_length_ms)

    print(f"Total length of the track: {total_length_ms} ms")
    print(f"Maximum delta between events: {max_delta_ms} ms")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: midi_tempo_stats.py <midi_file_path>")
        sys.exit(1)
    path = sys.argv[1]
    parse_midi_file(path)
