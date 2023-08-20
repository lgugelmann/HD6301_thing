;
; EDI: An OPL3 instrument editor
;

        cpu 6301

        include ../stdlib.inc

        SECTION edi
        PUBLIC edi_start

edi_ui:
        BINCLUDE edi_ui.txt
        byt $00                 ; Terminating 0, it's not in the .txt file

        ; An instrument description, takes 11 bytes, same format as
        ; include/instruments.inc
        reserve_memory instrument, 11

        zp_var edit_mode, 1     ; 0: editing OP1, 1: OP2, 2: Note to play
        zp_var note, 1          ; MIDI note that's played for the test tone
        zp_var cursor_column, 1 ; Edit cursor column position
        zp_var cursor_row, 1    ; Edit cursor row position
        zp_var playing, 1       ; 0 not playing, 1 playing

TREMOLO_OFFSET           = 0
VIBRATO_OFFSET           = 1
ENVELOPE_OFFSET          = 2
ATTACK_DECAY_OFFSET      = 3
FREQ_MULTIPLIER_OFFSET   = 4
VOLUME_VS_OCTAVE_OFFSET  = 5
TOTAL_ATTENUATION_OFFSET = 6
ATTACK_RATE_OFFSET       = 7
DECAY_RATE_OFFSET        = 8
SUSTAINED_LEVEL_OFFSET   = 9
RELEASE_RATE_OFFSET      = 10
WAVEFORM_OFFSET          = 11
FEEDBACK_OFFSET          = 12

        ; These are the individual operator settings broken out as bytes for
        ; easier editing. They are typically accessed through indexed ops off of
        ; current_op which points to either op1 or op2.
        reserve_memory op1, FEEDBACK_OFFSET+1
        ; No feedback in op2, so one shorter
        reserve_memory op2, FEEDBACK_OFFSET
        reserve_memory current_op, 2

OP1_COLUMN = 20
OP2_COLUMN = 69

NOTE_ROW = 7
TREMOLO_ROW           = 11
VIBRATO_ROW           = TREMOLO_ROW + VIBRATO_OFFSET
ENVELOPE_ROW          = TREMOLO_ROW + ENVELOPE_OFFSET
ATTACK_DECAY_ROW      = TREMOLO_ROW + ATTACK_DECAY_OFFSET
FREQ_MULTIPLIER_ROW   = TREMOLO_ROW + FREQ_MULTIPLIER_OFFSET
VOLUME_VS_OCTAVE_ROW  = TREMOLO_ROW + VOLUME_VS_OCTAVE_OFFSET
TOTAL_ATTENUATION_ROW = TREMOLO_ROW + TOTAL_ATTENUATION_OFFSET
ATTACK_RATE_ROW       = TREMOLO_ROW + ATTACK_RATE_OFFSET
DECAY_RATE_ROW        = TREMOLO_ROW + DECAY_RATE_OFFSET
SUSTAINED_LEVEL_ROW   = TREMOLO_ROW + SUSTAINED_LEVEL_OFFSET
RELEASE_RATE_ROW      = TREMOLO_ROW + RELEASE_RATE_OFFSET
WAVEFORM_ROW          = TREMOLO_ROW + WAVEFORM_OFFSET
FEEDBACK_ROW          = TREMOLO_ROW + FEEDBACK_OFFSET

EDIT_ROW_MIN = TREMOLO_ROW
EDIT_ROW_MAX = FEEDBACK_ROW

NOTE_MIN = 21
NOTE_MAX = 104

edi_start:
        clr GRAPHICS_CLEAR

        clr edit_mode           ; Set edit_mode to OP1
        clr playing
        lda #69                 ; A4, 440Hz
        sta note

        ldx #instruments_piano
        jsr instrument_to_zp_var
        ldx #op1
        stx current_op
        jsr draw_static_ui
        jsr draw_ui_values

        jsr edit_loop

        clr GRAPHICS_CLEAR
        rts

; A contains the byte to decode, X points to either op1 or op2.
op_byte0_to_zp:
        ; Byte 0: AM | VIB | EGT | KSR | 4bit MULT
        tab
        and b,#$0f
        stb FREQ_MULTIPLIER_OFFSET,x

        rol a                   ; rotate top bit into carry
        rol a                   ; and carry into lowest bit
        tab
        and b,#$01
        stb TREMOLO_OFFSET,x

        rol a                   ; and, stb do not touch C so we can rol more
        tab
        and b,#$01
        stb VIBRATO_OFFSET,x

        rol a
        tab
        and b,#$01
        stb ENVELOPE_OFFSET,x

        rol a
        and a,#$01
        sta ATTACK_DECAY_OFFSET,x

        rts

; A contains the byte to decode, X points to either op1 or op2.
op_byte1_to_zp:
        ; Byte 1: 2bit KSL | 6bit TL
        tab
        and b,#$3f
        stb TOTAL_ATTENUATION_OFFSET,x

        rol a
        rol a
        rol a
        and a,#$03
        sta VOLUME_VS_OCTAVE_OFFSET,x
        rts

; A contains the byte to decode, X points to either op1 or op2.
op_byte2_to_zp:
        ; Byte 2: 4bit AR | 4bit DR
        tab
        and b,#$0f
        stb DECAY_RATE_OFFSET,x

        lsr a
        lsr a
        lsr a
        lsr a
        sta ATTACK_RATE_OFFSET,x
        rts

; A contains the byte to decode, X points to either op1 or op2.
op_byte3_to_zp:
        ; Byte 3: 4bit SL | 4bit RR
        tab
        and b,#$0f
        stb RELEASE_RATE_OFFSET,x

        lsr a
        lsr a
        lsr a
        lsr a
        sta SUSTAINED_LEVEL_OFFSET,x
        rts

; Load the 11 byte instrument description pointed to by X
instrument_to_zp_var:
        ; push 11 bytes onto the stack to free up X
        lda #11
.loop:
        ldb 0,x
        psh b
        inx
        dec a
        bne .loop

        pul a
        ; Byte 10 is Channel setup, FB, CNT
        asr a
        and a,#$07
        sta op1+FEEDBACK_OFFSET

        ; Next up 5 bytes for op2 setup
        ldx #op2
.op_load:
        pul a
        ; Byte 4 or 9: op2 waveform
        sta WAVEFORM_OFFSET,x

        pul a
        ; Byte 3 or 8: op2 SL / RR
        jsr op_byte3_to_zp

        pul a
        ; Byte 2 or 7: op2 AR / DR
        jsr op_byte2_to_zp

        pul a
        ; Byte 1 or 6: op2 KSL / TL
        jsr op_byte1_to_zp

        pul a
        ; Byte 0 or 5: op2 AM..MULT
        jsr op_byte0_to_zp

        cpx #op1
        beq .end
        ; Next we have the same thing, but for op1
        ldx #op1
        bra .op_load
.end:
        rts

; X points to op1 or op2, A returns the encoded byte
zp_to_op_byte0:
        lda TREMOLO_OFFSET,x
        asl a
        ora VIBRATO_OFFSET,x
        asl a
        ora ENVELOPE_OFFSET,x
        asl a
        ora ATTACK_DECAY_OFFSET,x
        asl a
        asl a
        asl a
        asl a
        ora FREQ_MULTIPLIER_OFFSET,x
        rts

; X points to op1 or op2, A returns the encoded byte
zp_to_op_byte1:
        lda VOLUME_VS_OCTAVE_OFFSET,x
        ror a                   ; rotate into carry
        ror a
        ror a                   ; ...and then the top 2 bits
        ora TOTAL_ATTENUATION_OFFSET,x
        rts

; X points to op1 or op2, A returns the encoded byte
zp_to_op_byte2:
        lda ATTACK_RATE_OFFSET,x
        asl a
        asl a
        asl a
        asl a
        ora DECAY_RATE_OFFSET,x
        rts

; X points to op1 or op2, A returns the encoded byte
zp_to_op_byte3:
        lda SUSTAINED_LEVEL_OFFSET,x
        asl a
        asl a
        asl a
        asl a
        ora RELEASE_RATE_OFFSET,x
        rts

; X points to op1 or op2, A returns the encoded byte
zp_to_op_byte4:
        lda WAVEFORM_OFFSET,x
        rts

; X points to op1, A returns the encoded byte
zp_to_instrument_byte10:
        lda FEEDBACK_OFFSET,x
        asl a
        ora a,#$30
        rts

; Fills in the 'instrument' variable from the broken out zp variables
zp_var_to_instrument:
        ldx #op1
        jsr zp_to_op_byte0
        sta instrument
        jsr zp_to_op_byte1
        sta instrument+1
        jsr zp_to_op_byte2
        sta instrument+2
        jsr zp_to_op_byte3
        sta instrument+3
        lda WAVEFORM_OFFSET,x
        sta instrument+4
        jsr zp_to_instrument_byte10
        sta instrument+10

        ldx #op2
        jsr zp_to_op_byte0
        sta instrument+5
        jsr zp_to_op_byte1
        sta instrument+6
        jsr zp_to_op_byte2
        sta instrument+7
        jsr zp_to_op_byte3
        sta instrument+8
        lda WAVEFORM_OFFSET,x
        sta instrument+9
        rts

draw_static_ui:
        clr GRAPHICS_SET_ROW
        nop
        clr GRAPHICS_SET_COLUMN
        ldx #edi_ui
        jsr putstring
        rts

draw_ui_values:
        lda #1
        sta edit_mode
        jsr set_edit_column
        ldx #op2
        stx current_op
        jsr draw_full_operator

        dec edit_mode
        jsr set_edit_column
        ldx #op1
        stx current_op
        jsr draw_full_operator

        jsr draw_note

        lda #TREMOLO_ROW        ; Move back to top of op1 after draw
        sta GRAPHICS_SET_ROW
        sta cursor_row

        rts

; Assumes that cursor_column is set up to the correct operator
draw_full_operator:
        jsr draw_tremolo
        jsr draw_vibrato
        jsr draw_envelope
        jsr draw_attack_decay
        jsr draw_freq_multiplier
        jsr draw_volume_vs_octave
        jsr draw_total_attenuation
        jsr draw_attack_rate
        jsr draw_decay_rate
        jsr draw_sustained_level
        jsr draw_release_rate
        jsr draw_waveform
        tst edit_mode
        bne .end
        jsr draw_feedback
.end:
        rts

draw_note:
        lda #NOTE_ROW
        sta GRAPHICS_SET_ROW    ; We don't store the cursor row for note so we
                                ; can get back to the previously edited one.
        lda note
        jsr putchar_dec
        lda #-3
        sta GRAPHICS_MOVE_CURSOR
        rts

set_edit_column:
        tim #1,edit_mode
        beq .op1
        lda #OP2_COLUMN
        bra .end
.op1:
        lda #OP1_COLUMN
.end:
        sta GRAPHICS_SET_COLUMN
        sta cursor_column
        rts

draw_tremolo:
        lda #TREMOLO_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda TREMOLO_OFFSET,x
        bne .on
        ldx #off_string
        bra .end
.on:
        ldx #on_string
.end:
        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_vibrato:
        lda #VIBRATO_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda VIBRATO_OFFSET,x
        bne .on
        ldx #off_string
        bra .end
.on:
        ldx #on_string
.end:
        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_envelope:
        lda #ENVELOPE_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda ENVELOPE_OFFSET,x
        bne .on
        ldx #envelope_off_string
        bra .end
.on:
        ldx #envelope_on_string
.end:
        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_attack_decay:
        lda #ATTACK_DECAY_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda ATTACK_DECAY_OFFSET,x
        bne .on
        ldx #attack_decay_off_string
        bra .end
.on:
        ldx #attack_decay_on_string
.end:
        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_freq_multiplier:
        lda #FREQ_MULTIPLIER_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda FREQ_MULTIPLIER_OFFSET,x
        bne +                   ; value 0 -> 0.5
        pshx
        ldx #point_five_string
        jsr putstring
        pulx
        bra .end
+
        cmp a,#9                ; value 1..9 -> 1..9
        bgt +
        ldb #'0'
        aba
        bra .putchar_end
+
        tab                     ; Start drawing a '1' for the tens digit
        lda #'1'
        jsr putchar
        tba

        cmp a,#11               ; value 10, 11 -> draw 10
        bgt +
        lda #'0'
        bra .putchar_end
+
        cmp a,#13               ; 12, 13 -> 12
        bgt +
        lda #'2'
        bra .putchar_end
+
        lda #'5'                ; 14, 15 -> 15
.putchar_end:
        jsr putchar
        lda #' '                ; Extra spaces to cover up the '0.5' string
        jsr putchar
        jsr putchar
.end:
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_volume_vs_octave:
        lda #VOLUME_VS_OCTAVE_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda VOLUME_VS_OCTAVE_OFFSET,x

        ldb #VOLUME_VS_OCTAVE_LENGTH
        mul
        ldx #volume_vs_octave_strings
        abx

        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_total_attenuation:
        lda #TOTAL_ATTENUATION_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda TOTAL_ATTENUATION_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_attack_rate:
        lda #ATTACK_RATE_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda ATTACK_RATE_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_decay_rate:
        lda #DECAY_RATE_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda DECAY_RATE_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_sustained_level:
        lda #SUSTAINED_LEVEL_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda SUSTAINED_LEVEL_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_release_rate:
        lda #RELEASE_RATE_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda RELEASE_RATE_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_waveform:
        lda #WAVEFORM_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda WAVEFORM_OFFSET,x
        ldb #WAVEFORM_STRING_LENGTH
        mul                     ; A*B -> A:B. Low byte is in B.
        ldx #waveform_strings
        abx
        jsr putstring
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

draw_feedback:
        lda #FEEDBACK_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row

        ldx current_op
        lda FEEDBACK_OFFSET,x
        jsr putchar_dec
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

; Switch between OP1 / OP2 editing, or to OP1 if not editing an operator.
switch_operator:
        lda edit_mode
        bne .to_op1             ; If not in OP1, always move to OP1
        ldx #op2
        inc edit_mode           ; We're in OP1, move to OP2
        jsr set_edit_column
        lda cursor_row
        cmp a,#FEEDBACK_ROW     ; Feedback can only be edited in OP1
        bne .end
        dec a
        sta GRAPHICS_SET_ROW
        sta cursor_row
        bra .end
.to_op1:
        cmp a,#2                ; Are we in note mode?
        bne .just_colum_switch  ; If not, just switch columns
        lda #TREMOLO_ROW
        sta GRAPHICS_SET_ROW
        sta cursor_row
.just_colum_switch:
        clr edit_mode
        ldx #op1
.end:
        stx current_op
        jsr set_edit_column
        rts

; A contains KEY_RIGHT / KEY_LEFT
note_edit:
        cmp a,#KEY_RIGHT
        bne .left
        lda note
        inc a
        cmp a,#NOTE_MAX
        ble .end
        ; Past NOTE_MAX, wrap around
        lda #NOTE_MIN
        bra .end
.left:
        lda note
        dec a
        cmp a,#NOTE_MIN
        bgt .end
        ; Past NOTE_MAX, wrap around
        lda #NOTE_MAX
.end:
        sta note
        jsr draw_note
        rts

edit_line:
        ldb edit_mode
        cmp b,#2                ; Editing note
        bne .op
        jmp note_edit           ; rts there
.op:
        ldb cursor_row
        sub b,#EDIT_ROW_MIN     ; B is now the offset for the row we're editing

        ; Prepare a few values for X on the stack that we'll need.
        ; First up / last to be needed: the redraw function
        ldx #redraw_function_table
        abx                     ; abx twice as this table has 2-byte entries
        abx                     ; X is at redraw pointer for the current row
        pshx                    ; Store it for later

        ; Next a pointer to the zp variable for the value we're editing
        ldx current_op
        abx                     ; Now it points at the value we're changing
        pshx                    ; Store that pointer for later too

        ; Finally a pointer to the max value we can change the current row to
        ldx #max_value_table
        abx                     ; X points at the max value we can set

        cmp a,#KEY_RIGHT
        bne .down
        ldb 0,x                 ; Max value
        pulx
        lda 0,x                 ; The value we're changing
        cba                     ; Compare A,B. Sets flags for A-B.
        beq .at_max
        inc a
        bra .end
.at_max:                        ; A=B we're at the max, wrap around to 0
        lda #0
        bra .end
.down:
        ldb 0,x                 ; Max value
        pulx
        lda 0,x                 ; The value we're changing
        beq .at_min
        dec a
        bra .end
.at_min:
        tba
.end:
        sta 0,x                 ; Store the new value

        pulx                    ; The redraw function pointer
        ldx 0,x                 ; Load it
        jsr 0,x                 ; And jump to it

        rts

; A contains the up/down character
move_row:
        ldb edit_mode
        cmp b,#2
        beq .end                ; Note editing, no moving of rows

        cmp a,#KEY_UP
        bne .down
        ; Up
        lda cursor_row
        cmp a,#EDIT_ROW_MIN
        beq .at_min
        dec a                   ; Not at top, can go one row up
        bra .set_row
.at_min:
        lda #EDIT_ROW_MAX
        cmp b,#1                ; B is still edit_mode, #1 is OP2
        bne .set_row
        dec a                   ; OP2 has one fewer rows
        bra .set_row

.down:
        lda #EDIT_ROW_MAX
        ;  A-B->A. B is edit_mode so A is now the max row for the operator.
        sba
        ldb cursor_row
        cba
        beq .at_max
        tba
        inc a
        bra .set_row
.at_max:
        lda #EDIT_ROW_MIN
.set_row:
        sta GRAPHICS_SET_ROW
        sta cursor_row
.end:
        rts

toggle_note_edit:
        lda edit_mode
        cmp a,#2
        bne .enter_edit
        ldx current_op
        cpx #op1
        beq .to_op1

        lda #1                  ; To op2
        sta edit_mode
        lda cursor_row
        sta GRAPHICS_SET_ROW
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        rts

.to_op1:
        clr edit_mode
        lda cursor_row
        sta GRAPHICS_SET_ROW
        ; No need to set column, it's already correct
        rts
.enter_edit:
        lda #2
        sta edit_mode
        lda #NOTE_ROW
        sta GRAPHICS_SET_ROW
        lda #OP1_COLUMN
        sta GRAPHICS_SET_COLUMN
        rts

toggle_play:
        lda playing
        bne .stop
        inc playing             ; Playing to 1
        jmp play_note           ; Also play the note if enabling
.stop:
        dec playing             ; Playing to 0
        lda #1                  ; Channel
        jsr sound_stop_note
        rts

play_note:
        lda playing
        beq .not_playing

        lda #1                  ; Channel
        jsr sound_stop_note

        jsr zp_var_to_instrument
        lda #1                  ; Channel, zp_var_.. clobbers A
        ldx #instrument
        jsr sound_load_instrument
        ldb note
        jsr sound_play_note
.not_playing:
        rts

edit_loop:
.charloop:
        jsr getchar
        beq .charloop

        cmp a,#'q'
        bne +
        rts
+
        cmp a,#KEY_TAB
        bne +
        jsr switch_operator
        bra .charloop
+
        cmp a,#KEY_RIGHT
        bne +
        bra .left_right
+
        cmp a,#KEY_LEFT
        bne +
.left_right:
        jsr edit_line
        jsr play_note
        bra .charloop
+
        cmp a,#KEY_UP
        bne +
        bra .up_down
+
        cmp a,#KEY_DOWN
        bne +
.up_down:
        jsr move_row
        bra .charloop
+
        cmp a,#'n'
        bne +
        jsr toggle_note_edit
        bra .charloop
+
        cmp a,#' '
        bne +
        jsr toggle_play
        bra .charloop
+
        bra .charloop


; Common strings. The short ones need extra spaces to cover the longer ones when
; drawn over them.
off_string:
        byt "Off\0"
on_string:
        byt "On \0"
envelope_off_string:
        byt "No sustain\0"
envelope_on_string:
        byt "Sustain   \0"
attack_decay_off_string:
        byt "Stable with pitch\0"
attack_decay_on_string:
        byt "Higher with pitch\0"
point_five_string:
        byt "0.5\0"
; These need to all be exactly the same length as we're using index * length to
; get to the start of the string to print. Each individual string needs to be
; 0-terminated.
VOLUME_VS_OCTAVE_LENGTH = 15
volume_vs_octave_strings:
        byt "unchanged     \0"
        byt "-3 dB/octave  \0"
        byt "-1.5 dB/octave\0"  ; This weird order is as per the datasheet
        byt "-6 dB/octave  \0"
WAVEFORM_STRING_LENGTH = 15
waveform_strings:
        byt "Sine          \0"
        byt "Half-sine     \0"
        byt "Abs-sine      \0"
        byt "Pulse-abs-sine\0"
        byt "Even-only-sine\0"
        byt "Even-abs-sine \0"
        byt "Square        \0"
        byt "Derived square\0"

; A table with the maximum value that each variable can have
max_value_table:
        byt 1                   ; tremolo
        byt 1                   ; vibrato
        byt 1                   ; envelope
        byt 1                   ; attack_decay
        byt 15                  ; freq_multiplier
        byt 3                   ; volume_vs_octave
        byt 63                  ; total_attenuation
        byt 15                  ; attack_rate
        byt 15                  ; decay_rate
        byt 15                  ; sustained_level
        byt 15                  ; release_rate
        byt 7                   ; waveform
        byt 7                   ; feedback

; A table with pointers to the function to redraw a given value
redraw_function_table:
        adr draw_tremolo
        adr draw_vibrato
        adr draw_envelope
        adr draw_attack_decay
        adr draw_freq_multiplier
        adr draw_volume_vs_octave
        adr draw_total_attenuation
        adr draw_attack_rate
        adr draw_decay_rate
        adr draw_sustained_level
        adr draw_release_rate
        adr draw_waveform
        adr draw_feedback

        ENDSECTION
