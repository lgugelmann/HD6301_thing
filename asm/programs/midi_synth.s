        cpu 6301

        include ../stdlib.inc

        SECTION midi_synth
        PUBLIC midi_synth_start

MIDI_PLAY_NOTE             = $90
MIDI_STOP_NOTE             = $80
MIDI_POLYPHONIC_AFTERTOUCH = $A0
MIDI_CONTROL_CHANGE        = $B0
MIDI_PROGRAM_CHANGE        = $C0
MIDI_CHANNEL_AFTERTOUCH    = $D0
MIDI_PITCH_BEND            = $E0

NUM_CHANNELS = 18

        ; Some scratch space to store command data
        zp_var channel,1

midi_synth_start:
        clr GRAPHICS_CLEAR      ; Clear screen
        ldx #midi_synth_string
        jsr putstring

        ; Load a piano on all channels
        ldx #general_midi_instruments
        lda #NUM_CHANNELS
.instrument_setup_loop:
        jsr sound_load_instrument
        dec a
        bne .instrument_setup_loop

        jsr midi_synth
        rts                     ; Back to the monitor

midi_synth:
        jsr midi_uart_read_byte_blocking
        tab                     ; Updates status register, N is top bit state
        bpl .unexpected_data    ; N=1 command byte, N=0 data byte

        and a,#$F0              ; Top nibble = command, bottom = channel
        and b,#$0F
        stb channel

        cmp a,#MIDI_PLAY_NOTE
        bne +

        ; Read the MIDI note number
        jsr midi_uart_read_byte_blocking
        tab
        ; Read the third byte - amplitude
        jsr midi_uart_read_byte_blocking
        ;; jsr set_volume
        jsr play_note
        bra midi_synth
+
        cmp a,#MIDI_STOP_NOTE
        bne +
        ; Read the note to stop
        jsr midi_uart_read_byte_blocking
        tab
        ; Discard the third byte
        jsr midi_uart_read_byte_blocking
        jsr stop_note
        bra midi_synth
+
        cmp a,#MIDI_POLYPHONIC_AFTERTOUCH
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_uart_read_byte_blocking
        bra midi_synth
+
        cmp a,#MIDI_CONTROL_CHANGE
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_uart_read_byte_blocking
        bra midi_synth
+
        cmp a,#MIDI_PROGRAM_CHANGE
        bne +
        jsr midi_uart_read_byte_blocking
        jsr set_instrument
        bra midi_synth
+
        cmp a,#MIDI_CHANNEL_AFTERTOUCH
        bne +
        jsr midi_uart_read_byte_blocking
        bra midi_synth
+
        cmp a,#MIDI_PITCH_BEND
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_uart_read_byte_blocking
        bra midi_synth
+
        ; 'tab' updates status registers. We can use this to test the top bit:
        ; if set means MIDI status code, otherwise data byte.
        tab
        bpl .data_byte          ; branch if N=0
        lda #KEY_ENTER
        jsr putchar
.data_byte:
        tba
        jsr putchar_hex

.unexpected_data:
        ; TODO: put these onto a data buffer - there are commands that can show
        ; up in-between data bytes.
;        jsr putchar_hex

        jmp midi_synth
        ; rts

; Start playing the note number in B on the channel in 'channel'. The note needs
; to not already be playing.
play_note:
        lda channel
        inc a
        cmp a,#NUM_CHANNELS     ; Make sure we're not playing on channels beyond
                                ; what we support for now.
        bgt .end
        jmp sound_play_note     ; rts there
.end:
        rts

; Stop playing the note number in B
stop_note:
        lda channel
        inc a
        cmp a,#NUM_CHANNELS     ; Make sure we're not playing on channels beyond
                                ; what we support for now.
        bgt .end
        jmp sound_stop_midi_note     ; rts there
.end:
        rts

; A contains the volume, channel is in 'channel'. Keeps B unchanged.
set_volume:
        psh b
        tab
        ; MIDI velocity is 0..127, OPL3 attenuation 0..63. We can drop the
        ; lowest bit from velocity and invert it to get something roughly
        ; attenuation-shaped.
        lsr b
        eor b,#63
        lda channel
        jsr sound_set_attenuation
        pul b
        rts

; A contains the instrument number, channel is in 'channel'. Clobbers all registers.
set_instrument:
        ldb #11                 ; Each instrument is 11 bytes
        mul                     ; instrument number * 11
        addd #general_midi_instruments
        xgdx                    ; X now points to the instrument

        lda channel
        inc a
        cmp a,#NUM_CHANNELS     ; Make sure we're not playing on channels beyond
                                ; what we support for now.
        bgt .end

        ; MIDI channel 10 is always a rhythm channel
        cmp a,#10
        bne .not_rhythm
        ldb #128
        abx

.not_rhythm:
        jsr print_instrument
        jmp sound_load_instrument
        ; rts in sound_load_instrument
.end:
        rts

print_instrument:
        pshx
        psh a
        psh b

        pshx
        ldx #.instr_header
        jsr putstring
        pulx

        ldb #11
.loop:
        lda 0,x
        jsr putchar_hex
        lda #' '
        jsr putchar
        inx
        dec b
        bne .loop

        pul b
        pul a
        pulx
        rts
.instr_header:
        byt "\nAM KS AR SL WS AM KS AR SL WS FB\n\0"



midi_synth_string:
        byt "MIDI Synth:\n\0"

        ENDSECTION
