        ifndef __midi_inc
__midi_inc = 1

        cpu 6301

        include general_midi_instruments

        SECTION midi
        PUBLIC_SHARED midi_reset
        PUBLIC_SHARED midi_play_note
        PUBLIC_SHARED midi_stop_note
        PUBLIC_SHARED midi_program_change
        PUBLIC_SHARED midi_control_change

        ; Some ZP variables to exchange data
        PUBLIC_SHARED midi_channel
        PUBLIC_SHARED midi_note
        PUBLIC_SHARED midi_velocity

; Library for MIDI handling. Supports MIDI channels 1..16 with one instrument
; per channel and up to 18 notes polyphony across all channels.

; TODO:
; - Amplitude support
; - Pitch bend
; - Rework the code to use 0-based MIDI and OPL channel numbers everywhere
; - All notes off / all sounds off

; MIDI channel 10 is always rhythm instruments. Set to 9 as we're using 0-based
; channel numbers throughout.
MIDI_RHYTHM_CHANNEL = 9

NUM_OPL_CHANNELS = 18
NUM_MIDI_CHANNELS = 16

        zp_var midi_channel,1   ; MIDI channel for the current operation,
                                ; 0-based as in the wire format.
        zp_var midi_note,1      ; MIDI note for the current operation
        zp_var midi_velocity,1  ; MIDI note velocity for the current note
        zp_var midi_ksl_tl,1    ; KSL/TL byte for OP2 of the current instrument
        zp_var opl_channel,1    ; The OPL channel for the current operation
        zp_var midi_volume,1    ; Global volume setting

        ; Map from MIDI channel to GM instrument number. Percussion bank
        ; instruments are distinguished by having their top bit set.
        zp_var midi_channel_to_instrument,NUM_MIDI_CHANNELS

        ; Maps each OPL channel to the MIDI channel (first byte), note, and OP2
        ; KSL/TL byte it's set up for. An OPL channel is set up for a given MIDI
        ; channel if it has the right instrument loaded. A channel not playing
        ; anything is represented with 0 in the note byte. MIDI channel 10 is
        ; handled by using the note number as a fake 'MIDI channel'. So note 36
        ; on Channel 10 is represented here as 36,NN.
        zp_var opl_to_midi_channel_note,3*NUM_OPL_CHANNELS

midi_reset:
        clr a
        sta midi_channel
        sta midi_note
        sta midi_ksl_tl
        sta opl_channel
        lda #127
        sta midi_volume
        sta midi_velocity

        ; Load a piano on all channels
        ldx #general_midi_instruments
        lda #NUM_OPL_CHANNELS
.instrument_setup_loop:
        jsr sound_load_instrument
        dec a
        bne .instrument_setup_loop

        ldx #midi_channel_to_instrument
        lda #NUM_MIDI_CHANNELS
.midi_channel_setup_loop:
        clr 0,x
        inx
        dec a
        bne .midi_channel_setup_loop

        ldx #general_midi_instruments
        ldb 6,x                 ; KSL/TL byte for OP2
        ldx #opl_to_midi_channel_note
        lda #NUM_OPL_CHANNELS
.opl_channel_setup_loop:
        clr 0,x
        clr 1,x
        stb 2,x
        inx
        inx
        inx
        dec a
        bne .opl_channel_setup_loop
        rts

; Start playing the note number in 'midi_note' on the MIDI channel in
; 'midi_channel'.
midi_play_note:
        lda midi_channel
        cmp a,#NUM_MIDI_CHANNELS ; Make sure we're not playing on channels
                                 ; beyond what we support for now.
        bge .end

        cmp a,#MIDI_RHYTHM_CHANNEL
        bne .not_rhythm
        jmp play_rhythm

.not_rhythm:
        ; Find a free OPL3 channel to play this on. First pass we try to find a
        ; free OPL channel mapping to the right MIDI channel (and thus set up
        ; with the right instrument already).
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
        jsr set_instrument      ; After the call X points to the instrument
        lda 6,x                 ; OP2 KSL/TL byte
        pulx
        sta 2,x                 ; Store the KSL/TL byte
        lda opl_channel

        ; Here A contains the OPL channel number (0-based) to play on, X points
        ; to the opl_to_midi_channel_note map location for the OPL channel in A.
.found_empty_channel:
        sta opl_channel
        lda 2,x
        sta midi_ksl_tl
        jsr compute_volume
        tab
        lda opl_channel
        inc a                   ; OPL channels are 1-based for sound_play_note
        jsr sound_set_attenuation

        lda opl_channel
        inc a
        ldb midi_note
        stb 1,x
        jmp sound_play_note     ; rts there

.no_free_channel_error:
.end:
        rts

; Play a rhythm 'note' received on MIDI channel 10. That channel works
; differently: the MIDI note number is actually the percussion instrument to be
; played.
play_rhythm:
        ; This works similar to midi_play_note, however we use the MIDI note
        ; number as 'MIDI channel' number. The lowest GM percussion instrument
        ; number is 35 which is comfortably above the 16 MIDI channels we
        ; support so there is no collision. We also try put percussion
        ; instruments into high OPL3 channel numbers starting from top down to
        ; try to make finding channel matches more efficient. midi_play_note
        ; starts bottom up.

        ; Store the MIDI note number as channel, and look up the actual note
        ; number we want to use to get the right sound for the instrument.
        ldb midi_note
        stb midi_channel
        ldx #general_midi_percussion_note_number
        abx
        ldb 0,x
        stb midi_note

        ; First pass we try to find a free OPL channel set up for the right
        ; instrument number already.
        lda #NUM_OPL_CHANNELS-1
        ldx #opl_to_midi_channel_note + 3*(NUM_OPL_CHANNELS-1)
.free_channel_instrument_loop:
        ldb 0,x                 ; Load instrument number
        cmp b,midi_channel
        beq .found_channel
.no_match:
        tst a
        beq .no_channel_match
        dec a
        dex
        dex
        dex
        bra .free_channel_instrument_loop
.found_channel:
        ldb 1,x                  ; load note byte
        beq .found_empty_channel ; note is 0 -> found a channel to play on
        bra .no_match

        ; Second pass: find any free OPL channel and set it up correctly
.no_channel_match:
        lda #NUM_OPL_CHANNELS-1
        ldx #opl_to_midi_channel_note + 3*(NUM_OPL_CHANNELS-1)
.free_channel_loop:
        ldb 1,x                 ; Load note number
        beq .load_instrument
        tst a
        beq .no_free_channel_error
        dec a
        dex
        dex
        dex
        bra .free_channel_loop

        ; A contains the OPL channel number (0-based) to set up, X points to the
        ; opl_to_midi_channel_note map location for the OPL channel in A.
.load_instrument:
        pshx
        sta opl_channel
        lda midi_channel
        sta 0,x                 ; Save the new MIDI 'channel' for the OPL one
        ora #$80                ; Percussion instruments are 128 above regular
                                ; ones
        jsr set_instrument      ; After this X points to the instrument
        ldb 6,x                 ; OP2 KSL/TL byte
        lda opl_channel
        pulx
        stb 2,x
        ; Here A contains the OPL channel number (0-based) to play on, X points
        ; to the opl_to_midi_channel_note map location for the OPL channel in A.
.found_empty_channel:
        sta opl_channel
        lda 2,x
        sta midi_ksl_tl
        jsr compute_volume
        tab
        lda opl_channel
        inc a                   ; OPL channels are 1-based for sound_play_note
        jsr sound_set_attenuation

        lda opl_channel
        inc a                   ; OPL channels are 1-based for sound_play_note
        ldb midi_note
        stb 1,x
        jmp sound_play_note     ; rts there

.no_free_channel_error:
.end:
        rts

; Compute and set the correct volume for the 'midi_note' to be played at
; velocity 'midi_velocity' on an FM-modulated instrument with OP2 KSL/TL
; settings at 'midi_ksl_tl'. Volume is returned in A, clobbers B.
compute_volume:
        ; Algorithm:
        ; TL = 63 - ((63-patch TL)*2*(OPL3-scaled MIDI volume) / 128)
        ; 63 - ((instr volume)*(midi volume) >> 6)
        lda midi_ksl_tl
        and a,#$3f              ; Drop the KSL bits
        eor a,#$3f              ; 63-TL to get volume instead of attenuation.
        ; MIDI to OPL3 volume: M 0..31: M, 32..63: 16+M/2, 64..127: 32+M/4
        ; 32+M/4 can be decomposed as: 16 + (32 + M>>1)>>1
        ldb midi_velocity
        cmp b,#32
        blt .low
        cmp b,#64
        blt .mid
        lsr b
        add b,#32
.mid:
        lsr b
        add b,#16
.low:
        mul                     ; Result is in D=A|B
        asld                    ; (A|B >> 6) is equivalent to D << 2 and taking
                                ; the top byte.
        asld
        eor a,#$3f              ; 63-A
        ldb midi_ksl_tl
        and b,#$c0              ; Keep only the KSL bits
        aba                     ; Put computed TL and KSL back together
        rts

; Stop playing the MIDI note number in B on MIDI channel in 'midi_channel'.
midi_stop_note:
        lda midi_channel
        cmp a,#NUM_MIDI_CHANNELS ; Make sure we're not playing on channels
                                 ; beyond what we support for now.
        bge .end

        cmp a,#MIDI_RHYTHM_CHANNEL
        bne .not_rhythm
        jmp stop_rhythm

.not_rhythm:
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

; Stop playing the rhythm note number in B
stop_rhythm:
        stb midi_channel

        lda #NUM_OPL_CHANNELS-1
        ldx #opl_to_midi_channel_note + 3*(NUM_OPL_CHANNELS-1)
.loop:
        ldb 0,x
        cmp b,midi_channel
        beq .stop_note          ; We can't have more than one note per 'channel'
.no_match:
        tst a
        beq .error_no_channel_match
        dec a
        dex
        dex
        dex
        bra .loop

	; A counted up to the OPL channel, B contains the MIDI note, X points to
	; the opl_to_midi_channel_note map location for the OPL channel in B.
.stop_note:
        ldb 1,x                 ; Load the actual MIDI note being played
        clr 1,x
        inc a                    ; sound_* use 1-based OPL channel numbers
        jmp sound_stop_midi_note ; rts there

.error_no_channel_match:
.end:
        rts

; Stop playng all notes on the channel in 'midi_channel'
stop_all_channel_notes:
        lda midi_channel
        cmp a,#MIDI_RHYTHM_CHANNEL
        beq .stop_rhythm

        lda #1                  ; Counting 1-based OPL-channel numbers here
        ldx #opl_to_midi_channel_note
.loop:
        ldb 0,x
        cmp b,midi_channel
        bne .continue
        ldb 1,x
        beq .continue           ; No note playing on this OPL channel
        clr 1,x                 ; Clear note to mark it as stopped
        jsr sound_stop_note
.continue:
        inx
        inx
        inx
        inc a
        cmp a,#NUM_OPL_CHANNELS
        ble .loop
        rts

.stop_rhythm:
        lda #1                  ; Counting 1-based OPL-channel numbers here
        ldx #opl_to_midi_channel_note
.rhythm_loop:
        ldb 0,x
        cmp b,#NUM_MIDI_CHANNELS
        ble .rhythm_continue
        ldb 1,x
        beq .rhythm_continue    ; No note playing on this OPL channel
        clr 1,x                 ; Clear note to mark it as stopped
        jsr sound_stop_note
.rhythm_continue:
        inx
        inx
        inx
        inc a
        cmp a,#NUM_OPL_CHANNELS
        ble .rhythm_loop
        rts


; Sets the OPL channel in 'opl_channel' to the instrument number in A. Clobbers
; A, B. X points to the instrument definition after the call.
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
midi_program_change:
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
        inx
        bra .loop
.channel_match:
        ldb 1,x
        beq .not_playing
        ; A note is playing while we're changing instrument. I guess we stop it?
        ; TODO: figure out what the standard says.
        pshx
        psh a
        jsr midi_stop_note
        pul a
        pulx
.not_playing:
        ldb #$FF
        stb 0,x                 ; FF is an invalid channel number
        bra .no_match

.error_channel_overflow:
.end:
        rts

; Handles MIDI control change messages. The control number is passed in B, the
; data byte in A.
midi_control_change:

; MIDI Control Change numbers
MIDI_CONTROL_CHANNEL_VOLUME     = $07
MIDI_CONTROL_SOUND_CONTROLLER_1 = $46
; Channel mode messages
MIDI_CONTROL_ALL_SOUND_OFF      = $78
MIDI_CONTROL_ALL_NOTES_OFF      = $7B

        ; Sound Controller 1, set it up as PC for easy instrument switching
        cmp b,#MIDI_CONTROL_SOUND_CONTROLLER_1
        bne +
        jmp midi_program_change ; jsr there
+
        cmp b,#MIDI_CONTROL_ALL_NOTES_OFF
        bne +
        jmp stop_all_channel_notes ; jsr there
+
        cmp b,#MIDI_CONTROL_ALL_SOUND_OFF
        bne +
        jmp stop_all_channel_notes ; jsr there
+
        cmp b,#MIDI_CONTROL_CHANNEL_VOLUME
        bne +
        ; Technically this should be per channel - global only for now
        sta midi_volume
        rts
+
        ; Unhandled CC message
        rts

midi_synth_string:
        byt "MIDI Synth:\n\0"

        ENDSECTION

        endif
