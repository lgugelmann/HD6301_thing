        cpu 6301

        include ../stdlib.inc

        SECTION midi_decode
        PUBLIC midi_decode_start

        reserve_memory file_buffer, 512

midi_decode_start:
        ; Get the parameter string. It should contain a MIDI file name.
        tsx
        ldx 2,x                 ; Get the parameter string
        lda 0,x                 ; First char of the string
        beq error_no_file

        ldd #file_buffer
        jsr file_open
        tst a                   ; a > 127 --> N is set
        bmi error_file_open
        psh a                   ; put the FD on the stack

        jsr file_read
        tst a                   ; a > 127 --> N is set
        bmi error_file_read

        jsr midi_decode

        pul a
        jmp file_close
        ; rts

error_no_file:
        ldx #.usage_string
        jmp putstring
.usage_string:
        byt "Usage: midi_decode FILE.MID\n\0"

error_file_open:
        ldx #.file_open_error
        jmp putstring
.file_open_error:
        byt "Failed to open file\n\0"

error_file_read:
        ldx #.file_read_error
        jmp putstring
.file_read_error:
        byt "Failed to read file\n\0"

midi_decode:
        ldx #file_buffer

        ; MIDI files start with 4 bytes matching "MThd"
        lda 0,x
        cmp a,#"M"
        bne .error_midi_decoding

        lda 1,x
        cmp a,#"T"
        bne .error_midi_decoding

        lda 2,x
        cmp a,#"h"
        bne .error_midi_decoding

        lda 3,x
        cmp a,#"d"
        bne .error_midi_decoding

        ; Next is the header size in bytes as a 4-byte number (high first). It's
        ; always 6. Sanity check that we get a 6 at the right place.
        lda 7,x
        cmp a,#$06
        bne .error_midi_decoding

        ; Read the format. We only support format 0 (a single track). It's a
        ; 16-bit number but we only look at the low byte here.
        ldd 8,x
        bne .error_midi_format

        ; Number of tracks has to be 1 for format 0. Sanity check that.
        ldd 10,x
        cmp a,#0
        bne .error_midi_format
        cmp b,#1
        bne .error_midi_format

        ; Next are the 2 bytes for time division. Skip those for now.

        ; Skip over the header
        ldb #14
        abx

        ; Read the track header
        lda 0,x
        cmp a,#"M"
        bne .error_midi_decoding

        lda 1,x
        cmp a,#"T"
        bne .error_midi_decoding

        lda 2,x
        cmp a,#"r"
        bne .error_midi_decoding

        lda 3,x
        cmp a,#"k"
        bne .error_midi_decoding

        ; We interleave the error handling code here to avoid long jumps
        bra +
.error_midi_decoding:
        ldx #decode_error_string
        jmp putstring

.error_midi_format:
        ldx #format_error_string
        jmp putstring
+

        ; Move X past the track header
        ldb #4
        abx

        ; Next are 4 bytes of track length. Our system can't deal with files
        ; bigger than 64k, so we can ignore the topmost two bytes. The data is
        ; big endian, so we can read it straight from the file.
        ldd 2,x

        ; We have 512 - (size of header until now) many bytes ready to be read.
        ; TDOO: fix the assumption that the MIDI file is >512 bytes long.
        subd #512-22

        ; Put the remaining total track length onto the stack
        xgdx
        pshx

        ; Put the remaining track bytes that are in the file buffer on the stack
        ldx #512-22
        pshx

        xgdx                    ; X is back to pointing into the file buffer
        ldb #3                  ; Only 3 here because byte_to_a will do an inx later
        abx                     ; Skip "4" length bytes

byte_to_a macro
        inx
        cpx #file_buffer+512
        bne .byte_to_a
        jmp .end
.byte_to_a:
        lda 0,x
        endm

byte_to_b macro
        inx
        cpx #file_buffer+512
        bne .byte_to_b
        jmp .end
.byte_to_b:
        ldb 0,x
        endm

decode_variable_length macro
        ; Decode the next delta. We assume it's 2 bytes at most.
        clr a
        byte_to_b
        bpl .decode_variable_length_end ; Top bit not set, only one byte, done.
        ; We need to compute ((B & $7f) << 7) + nextB. We do this by putting B
        ; into A, shifting A right with the top bit into carry, and adding the
        ; carry to B.
        tba
        and a,#$7f
        byte_to_b
        bpl .decode_variable_length_delta_ok
        jmp .error_delta_too_large
.decode_variable_length_delta_ok:
        lsra                    ; Bottom bit of A -> C
        adc b,#0                ; Add the carry to B
.decode_variable_length_end:
        endm

        ; More error code interleaving for short branch distances
        bra .decoder_loop
.error_delta_too_large:
        ldx #delta_too_large_error_string
        jsr putstring
        drop2
        drop2
        rts

.error_event_too_large:
        ldx #event_too_large_error_string
        jsr putstring
        drop2
        drop2
        rts
        ; Start decoding. Each event starts with a variable-length-encoded time
        ; delta, then a payload.
.decoder_loop:
        lda #KEY_ENTER
        jsr putchar
        decode_variable_length
        ; TODO: do something with the delta
        psh a
        lda #"D"
        jsr putchar
        pul a
        jsr putchar_hex
        tba
        jsr putchar_hex
        lda #" "
        jsr putchar

        ; Get the event type into A
        byte_to_a
        jsr putchar_hex

        ; Handle the various event types. TODO: sort them by frequency for max
        ; performance.
        cmp a,#$ff              ; Meta event
        bne +
        lda #" "
        jsr putchar
        byte_to_a               ; Event type
        ; TODO handle event type
        jsr putchar_hex
        decode_variable_length
        jsr putchar_hex
        tba
        jsr putchar_hex
.event_skip_loop:
        ; TODO optimize this
        byte_to_a
        dec b
        bne .event_skip_loop
        jmp .decoder_loop
+
        cmp a,#$7f              ; Sysex
        beq .sysex
        cmp a,#$f0
        bne +
.sysex:
        lda #" "
        jsr putchar
        decode_variable_length
        jsr putchar_hex
        tba
        jsr putchar_hex
.event_skip_loop2:
        ; TODO optimize this
        byte_to_a
        dec b
        bne .event_skip_loop2
        jmp .decoder_loop
+

.end:

        drop2                   ; remaining buffer bytes
        drop2                   ; total track length
        rts

format_error_string:
        byt "Error: we only support single-track files\n\0"
decode_error_string:
        byt "Failed to decode file\n\0"
delta_too_large_error_string:
        byt "Delta too large\n\0"
event_too_large_error_string:
        byt "Event too big: max supported is 127 bytes\n\0"


        ENDSECTION