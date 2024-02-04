#!/usr/bin/python3

import mido
import sys

class ChannelStats:
    def __init__(self, channel):
        self.channel = channel
        self.on_count = 0
        self.total_notes = 0
        self.max_notes = 0
        self.current_program = -1
        self.program_note_count = 0
        self.programs = []
        pass

    def __str__(self):
        ret = f"Channel: {self.channel}, notes: {self.total_notes}, " \
            f"max simultaneous: {self.max_notes}"
        for program in self.programs:
            ret += f"\n  program: {program[0]}, notes: {program[1]}"
        ret += f"\n  program: {self.current_program}, " \
            f"notes: {self.program_note_count}"
        return ret

    def on(self, note):
        self.on_count += 1
        self.total_notes += 1
        self.program_note_count += 1
        if self.on_count > self.max_notes:
            self.max_notes = self.on_count

    def off(self, note):
        self.on_count -= 1
        assert(self.on_count >= 0)

    def pc(self, program):
        if self.current_program != program and self.current_program != -1:
            self.programs += [(self.current_program, self.program_note_count)]
        self.current_program = program

class Channel10Stats:
    def __init__(self):
        self.on_count = 0
        self.total_notes = 0
        self.max_notes = 0
        self.note_changes = 0
        self.prev_note = 0
        self.notes = {}
        pass

    def __str__(self):
        ret = f"Channel 10: notes: {self.total_notes}, " \
            f"max simultaneous: {self.max_notes}, " \
            f"note changes: {self.note_changes}"
        for note in sorted(self.notes):
            ret += f"\n  note: {note} played: {self.notes[note]} times"
        return ret

    def on(self, note):
        self.on_count += 1
        self.total_notes += 1
        if self.on_count > self.max_notes:
            self.max_notes = self.on_count
        if note not in self.notes:
            self.notes[note] = 0;
        self.notes[note] += 1
        if self.prev_note != note:
            self.note_changes += 1
            self.prev_note = note

    def off(self, note):
        self.on_count -= 1
        assert(self.on_count >= 0)

    def pc(self, program):
        print(f"Unexpected Chan 10 prog change to {program}")
        pass

class GlobalStats:
    def __init__(self):
        self.on_count = 0
        self.total_notes = 0
        self.max_notes = 0
        pass

    def __str__(self):
        ret = f"Total notes: {self.total_notes}, " \
            f"max simultaneous: {self.max_notes}"
        return ret

    def on(self):
        self.on_count += 1
        self.total_notes += 1
        if self.on_count > self.max_notes:
            self.max_notes = self.on_count

    def off(self):
        self.on_count -= 1
        assert(self.on_count >= 0)



if __name__ == '__main__':
    if len(sys.argv) > 3:
        print(f"usage: {sys.argv[0]} in.mid [out.mid]")
        print(sys.argv)
        exit(-1)

    mid = mido.MidiFile(sys.argv[1])

    for i, track in enumerate(mid.tracks):
        print('Track {}: {}'.format(i, track.name))
        overall_stats = GlobalStats()
        stats = {}
        stats[9] = Channel10Stats()
        for index, msg in enumerate(track):
            if msg.type == 'note_on':
                if msg.channel not in stats:
                    stats[msg.channel] = ChannelStats(msg.channel+1)
                stats[msg.channel].on(msg.note)
                overall_stats.on()
                continue
            if msg.type == 'note_off':
                if msg.channel not in stats:
                    stats[msg.channel] = ChannelStats(msg.channel+1)
                stats[msg.channel].off(msg.note)
                overall_stats.off()
                continue
            if msg.type == 'program_change':
                if msg.channel not in stats:
                    stats[msg.channel] = ChannelStats(msg.channel+1)
                stats[msg.channel].pc(msg.program)
                continue

        print(overall_stats)
        for k in sorted(stats.keys()):
            print(stats[k])
