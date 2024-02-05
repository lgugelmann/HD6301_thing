        cpu 6301

        include ../stdlib.inc

        SECTION midi_synth
        PUBLIC midi_synth_start

; MIDI synth supporting MIDI channels 1..16 with one instrument per channel and
; up to 18 notes polyphony across all channels.
; TODO:
; - Amplitude support
; - Pitch bend
; - Proper rhythm channel support
; - Rework the code to use 0-based MIDI and OPL channel numbers everywhere
; - All notes off / all sounds off

MIDI_PLAY_NOTE             = $90
MIDI_STOP_NOTE             = $80
MIDI_POLYPHONIC_AFTERTOUCH = $A0
MIDI_CONTROL_CHANGE        = $B0
MIDI_PROGRAM_CHANGE        = $C0
MIDI_CHANNEL_AFTERTOUCH    = $D0
MIDI_PITCH_BEND            = $E0

; MIDI channel 10 is always rhythm instruments. Set to 9 as we're using 0-based
; channel numbers throughout.
MIDI_RHYTHM_CHANNEL = 9

NUM_OPL_CHANNELS = 18
NUM_MIDI_CHANNELS = 16

        zp_var midi_channel,1   ; MIDI channel for the current operation,
                                ; 0-based as in the wire format.
        zp_var midi_note,1      ; MIDI note for the current operation
        zp_var opl_channel,1    ; The OPL channel for the current operation

        ; Map from MIDI channel to GM instrument number. Percussion bank
        ; instruments are distinguished by having their top bit set.
        zp_var midi_channel_to_instrument,NUM_MIDI_CHANNELS

        ; Maps each OPL channel to the MIDI channel (first byte) and note
        ; (second byte) it's set up for. An OPL channel is set up for a given
        ; MIDI channel if it has the right instrument loaded. A channel not
        ; playing anything is represented with 0 in the note byte. MIDI channel
        ; 10 is handled by using the note number as a fake 'MIDI channel'. So
        ; note 36 on Channel 10 is represented here as 36,NN.
        zp_var opl_to_midi_channel_note,2*NUM_OPL_CHANNELS

midi_synth_start:
        clr GRAPHICS_CLEAR      ; Clear screen
        ldx #midi_synth_string
        jsr putstring

        ; Load a piano on all channels
        ldx #general_midi_instruments
        lda #NUM_OPL_CHANNELS
.instrument_setup_loop:
        jsr sound_load_instrument
        dec a
        bne .instrument_setup_loop

        ldx #midi_channel_to_instrument
        clr a
.midi_channel_setup_loop:
        clr 0,x
        inx
        inc a
        cmp a,#NUM_MIDI_CHANNELS
        blt .midi_channel_setup_loop

        ldx #opl_to_midi_channel_note
        clr a
.opl_channel_setup_loop:
        clr 0,x
        inx
        clr 0,x
        inx
        inc a
        cmp a,#NUM_OPL_CHANNELS
        blt .opl_channel_setup_loop

        jsr midi_synth
        rts                     ; Back to the monitor

debug_print:
        ldx #.debug_print_midi_map
        jsr putstring
        ldx #midi_channel_to_instrument
        ldb #NUM_MIDI_CHANNELS
.midi_loop:
        lda 0,x
        jsr putchar_hex
        inx
        dec b
        bne .midi_loop

        ldx #.debug_print_opl_map
        jsr putstring
        ldx #opl_to_midi_channel_note
        ldb #NUM_OPL_CHANNELS
.opl_loop:
        lda 0,x
        jsr putchar_hex
        inx
        lda 0,x
        jsr putchar_hex
        inx
        lda " "
        jsr putchar
        dec b
        bne .opl_loop

        rts

.debug_print_midi_map:
        byt "\n\nMIDI channel to instrument:\n\0"
.debug_print_opl_map:
        byt "\nOPL to MIDI/note:\n\0"

midi_synth:
        ;; jsr debug_print
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
        cmp a,#70               ; Sound Controller 1, set it up as PC
        bne .not_70
        jsr midi_uart_read_byte_blocking
        jsr program_change
        bra midi_synth
.not_70:
        jsr midi_uart_read_byte_blocking
        bra midi_synth
+
        cmp a,#MIDI_PROGRAM_CHANGE
        bne +
        jsr midi_uart_read_byte_blocking
        jsr program_change
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
        jsr putchar_hex

        jmp midi_synth
        ; rts

; Start playing the note number in 'midi_note' on the MIDI channel in
; 'midi_channel'.
play_note:
        lda midi_channel
        cmp a,#NUM_MIDI_CHANNELS ; Make sure we're not playing on channels
                                 ; beyond what we support for now.
        bge .end

        ; TODO: add support for rhythm
        cmp a,#MIDI_RHYTHM_CHANNEL
        beq .end

        ; Find a free OPL3 channel to play this on. First pass we try to find a
        ; free OPL channel mapping to the right MIDI channel.
        clr a                   ; A keeps track of the OPL channel number
        ldx #opl_to_midi_channel_note
.free_channel_instrument_loop:
        ldb 0,x                 ; Load MIDI channel
        cmp b,midi_channel
        beq .found_channel
.no_match:
        inc a
        cmp a,#NUM_OPL_CHANNELS
        beq .no_channel_match
        inx
        inx
        bra .free_channel_instrument_loop
.found_channel:
        ldb 1,x                  ; load note byte
        beq .found_empty_channel ; note is 0 -> found a channel to play on
        bra .no_match

        ; Second pass: find any free OPL channel and set it up correctly
.no_channel_match:
        clr a
        ldx #opl_to_midi_channel_note
.free_channel_loop:
        ldb 1,x                 ; Load note number
        beq .load_instrument
        inc a
        cmp a,#NUM_OPL_CHANNELS
        beq .no_free_channel_error
        inx
        inx
        bra .free_channel_loop

        ; A contains the OPL channel number (0-based) to set up, X points to the
        ; opl_to_midi_channel_note map location for the OPL channel in A.
.load_instrument:
        pshx
        sta opl_channel
        ldb midi_channel
        stb 0,x                 ; Save the new MIDI channel for the OPL one
        ldx #midi_channel_to_instrument
        abx
        lda 0,x                 ; Load the instrument number into A
        jsr set_instrument

        lda opl_channel
        pulx
        ; Here A contains the OPL channel number (0-based) to play on, X points
        ; to the opl_to_midi_channel_note map location for the OPL channel in A.
.found_empty_channel:
        inc a                   ; OPL channels are 1-based for sound_play_note
        ldb midi_note
        stb 1,x
        jmp sound_play_note     ; rts there

.no_free_channel_error:
.end:
        rts

; Stop playing the MIDI note number in B on MIDI channel in 'midi_channel'.
stop_note:
        lda midi_channel
        cmp a,#NUM_MIDI_CHANNELS ; Make sure we're not playing on channels
                                 ; beyond what we support for now.
        bge .end

        stb midi_note

        clr a
        ldx #opl_to_midi_channel_note
.loop:
        ldb 0,x
        cmp b,midi_channel
        beq .channel_match
.no_match:
        inc a
        cmp a,#NUM_OPL_CHANNELS
        beq .error_no_channel_match
        inx
        inx
        bra .loop
.channel_match:
        ldb 1,x
        cmp b,midi_note
        beq .stop_note
        bra .no_match

	; A counted up to the OPL channel, B contains the MIDI note, X points to
	; the opl_to_midi_channel_note map location for the OPL channel in B.
.stop_note:
        clr 1,x
        inc a                    ; sound_* use 1-based OPL channel numbers
        jmp sound_stop_midi_note ; rts there

.error_no_channel_match:
.end:
        rts

; Sets the OPL channel in 'opl_channel' to the instrument number in A. Clobbers
; all registers.
set_instrument:
        ldb #11                 ; Each instrument is 11 bytes
        mul                     ; instrument number * 11
        addd #general_midi_instruments
        xgdx                    ; X now points to the instrument
        lda opl_channel
        inc a                   ; sound_* use 1-based channel numbers
        jmp sound_load_instrument
        ; rts in sound_load_instrument

; Changes MIDI channel 'midi_channel' to the instrument in A. Stops all notes
; playing on that channel.
program_change:
        ; Store new setting in MIDI channel to instrument map
        ldx #midi_channel_to_instrument
        ldb midi_channel
        abx
        sta 0,x

        ; All existing OPL channels on the previous setting are now
        ; misconfigured.
        clr a
        ldx #opl_to_midi_channel_note
.loop:
        ldb 0,x
        cmp b,midi_channel
        beq .channel_match
.no_match:
        inc a
        cmp a,#NUM_OPL_CHANNELS
        beq .end
        inx
        inx
        bra .loop
.channel_match:
        ldb 1,x
        beq .not_playing
        ; A note is playing while we're changing instrument. I guess we stop it?
        ; TODO: figure out what the standard says.
        pshx
        psh a
        jsr stop_note
        pul a
        pulx
.not_playing:
        ldb #$FF
        stb 0,x                 ; FF is an invalid channel number
        bra .no_match

.error_channel_overflow:
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
