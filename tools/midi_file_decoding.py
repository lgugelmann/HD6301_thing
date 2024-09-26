#!/usr/bin/env python3

import sys

def read_variable_length(data, start_offset):
  result = 0
  offset = start_offset
  while True:
    byte = data[offset]
    result = (result << 7) | (byte & 0x7F)
    offset += 1
    if not (byte & 0x80):
      break
  return result, offset

def decode_midi(file_path):
  with open(file_path, 'rb') as file:
    data = file.read()

  # MIDI header chunk
  header_chunk_id = data[0:4].decode()
  header_chunk_size = int.from_bytes(data[4:8], byteorder='big')
  format_type = int.from_bytes(data[8:10], byteorder='big')
  num_tracks = int.from_bytes(data[10:12], byteorder='big')
  time_division = int.from_bytes(data[12:14], byteorder='big')

  print(f"Header Chunk ID: {header_chunk_id}")
  print(f"Header Chunk Size: {header_chunk_size}")
  print(f"Format Type: {format_type}")
  print(f"Number of Tracks: {num_tracks}")
  if (time_division & 0x8000) == 0:
    print(f"Time Division: {time_division} ticks per beat")
  else:
    print(f"Time Division: {time_division & 0x7FFF} frames per second")

  # Assuming a single track for simplicity
  if format_type != 0:
    print("Only format type 0 is supported")
    return

  track_chunk_id = data[14:18].decode()
  track_chunk_size = int.from_bytes(data[18:22], byteorder='big')
  print(f"Track Chunk ID: {track_chunk_id}")
  print(f"Track Chunk Size: {track_chunk_size} / {track_chunk_size:04x} hex")

  # Start decoding MIDI events
  offset = 22  # Starting offset of the track data
  events_decoded = 0
  max_delta_time = 0
  while offset < len(data):
    delta_time, offset = read_variable_length(data, offset)
    max_delta_time = max(max_delta_time, delta_time)
    event_type = data[offset]
    offset += 1

    if event_type == 0xFF:  # Meta event
      meta_type = data[offset]
      offset += 1
      length, offset = read_variable_length(data, offset)
      event_data = data[offset:offset+length]
      offset += length
      print(f"Meta Event: Delta {delta_time}, Type {meta_type:02x}, Length {length}")
      if meta_type <= 0x0f:
        text = event_data.decode()
        print(f"  Text: {text}")
      elif meta_type == 0x51:
        tempo = int.from_bytes(event_data, byteorder='big')
        print(f"  Tempo: {tempo} microseconds per beat")
      elif meta_type == 0x58:
        numerator = event_data[0]
        denominator = 2 ** event_data[1]
        print(f"  Time Signature: {numerator}/{denominator}")
      elif meta_type == 0x2F:
        print("  End of Track")
      else:
        print(f"  Unknown meta event type: {meta_type:02x}")
    elif event_type >= 0x80 and event_type < 0xF0:  # MIDI event
      channel = event_type & 0x0F
      status = event_type & 0xF0
      if status in [0x80, 0x90, 0xA0, 0xB0, 0xE0]:  # Two data bytes
        data1 = data[offset]
        data2 = data[offset+1]
        offset += 2
        print(f"MIDI Event: Delta {delta_time}, Status {status:02x}, Channel {channel:02x}, Data1 {data1:02x}, Data2 {data2:02x}")
      else:  # One data byte
        data1 = data[offset]
        offset += 1
        print(f"MIDI Event: Delta {delta_time}, Status {status:02x}, Channel {channel}, Data1 {data1:02x}")
    elif event_type == 0xF0:
      length, offset = read_variable_length(data, offset)
      print(f"SysEx Event: Delta {delta_time}, Length {length}")
      event_data = data[offset:offset+length]
      offset += length
    else:
      print(f"Unknown event type: {event_type:02x}, Delta {delta_time}")
      continue

    events_decoded += 1
  print(f"Decoded {events_decoded} events, max delta time: {max_delta_time}")

if __name__ == '__main__':
  if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} midi_file.mid")
    sys.exit(1)
  decode_midi(sys.argv[1])
