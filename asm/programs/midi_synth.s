        cpu 6301

        include ../stdlib.inc

        SECTION midi_synth
        PUBLIC midi_synth_start

; MIDI synth supporting MIDI channels 1..16 with one instrument per channel and
; up to 18 notes polyphony across all channels.
; TODO:
; - Amplitude support
; - Pitch bend
; - Rework the code to use 0-based MIDI and OPL channel numbers everywhere
; - All notes off / all sounds off

MIDI_PLAY_NOTE             = $90
MIDI_STOP_NOTE             = $80
MIDI_POLYPHONIC_AFTERTOUCH = $A0
MIDI_CONTROL_CHANGE        = $B0
MIDI_PROGRAM_CHANGE        = $C0
MIDI_CHANNEL_AFTERTOUCH    = $D0
MIDI_PITCH_BEND            = $E0

midi_synth_start:
        clr GRAPHICS_CLEAR      ; Clear screen
        ldx #synth_string
        jsr putstring
        jsr midi_reset
        jsr synth
        rts                     ; Back to the monitor

synth:
        jsr midi_uart_read_byte_blocking
        tab                     ; Updates status register, N is top bit state
        bpl .unexpected_data    ; N=1 command byte, N=0 data byte

        and a,#$F0              ; Top nibble = command, bottom = channel
        and b,#$0F
        stb midi_channel

        cmp a,#MIDI_PLAY_NOTE
        bne +

        ; Read the MIDI note number
        jsr midi_uart_read_byte_blocking
        sta midi_note
        ; Read the third byte - velocity
        jsr midi_uart_read_byte_blocking
        sta midi_velocity
        jsr midi_play_note
        bra synth
+
        cmp a,#MIDI_STOP_NOTE
        bne +
        ; Read the note to stop
        jsr midi_uart_read_byte_blocking
        tab
        ; Discard the third byte
        jsr midi_uart_read_byte_blocking
        jsr midi_stop_note
        bra synth
+
        cmp a,#MIDI_POLYPHONIC_AFTERTOUCH
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_uart_read_byte_blocking
        bra synth
+
        cmp a,#MIDI_CONTROL_CHANGE
        bne +
        jsr midi_uart_read_byte_blocking
        tab
        jsr midi_uart_read_byte_blocking
        jsr midi_control_change
        bra synth
+
        cmp a,#MIDI_PROGRAM_CHANGE
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_program_change
        bra synth
+
        cmp a,#MIDI_CHANNEL_AFTERTOUCH
        bne +
        jsr midi_uart_read_byte_blocking
        bra synth
+
        cmp a,#MIDI_PITCH_BEND
        bne +
        jsr midi_uart_read_byte_blocking
        jsr midi_uart_read_byte_blocking
        bra synth
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
        jsr putchar_hex

        jmp synth
        ; rts

synth_string:
        byt "MIDI Synth:\n\0"

        ENDSECTION
