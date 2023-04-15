        ifndef __ps2_decoder_inc
__ps2_decoder_inc = 1

        include macros

        SECTION ps2_decoder
        ; Define the keymap variables inside the ps2_decoder section to avoid
        ; potential conflicts. This should be the only use of them.
        include keyboard_keymap_us

        PUBLIC ps2_decoder_init
        PUBLIC ps2_decoder_decode

; Flags for the ps2 state machine
KEY_UP   = %10000000 ; The next scancode indicates a key-up
SHIFTED  = %01000000 ; Shift is being held
EXTENDED = %00100000 ; Scancode is a control code from extended set
        zp_var flags,1

ps2_decoder_init:
        clr flags
        rts

; Decodes the next byte of a stream of ps2 keycodes passed in in B. Returns in A
; either an 'ASCII'-ish code or 0. Clobbers B and X.
ps2_decoder_decode:
        ; Decode input
        cmp b,#$f0              ; key up marker
        beq .handle_key_up_marker
        cmp b,#$12              ; left shift
        beq .handle_shift
        cmp b,#$59              ; right shift
        beq .handle_shift
        cmp b,#$e0              ; extended character code marker
        beq .handle_extended_marker

        tim #KEY_UP,flags       ; character released
        beq .not_key_up

        ; clear KEY_UP and EXTENDED - the latter is only set for extended codes
        ; but clearing it always doesn't hurt.
        aim #($ff - KEY_UP - EXTENDED),flags
        bra .end

.not_key_up:
        tim #SHIFTED,flags
        bne .use_shifted_table

        tim #EXTENDED,flags
        bne .use_extended_table

        ; use regular keycode table
        ldx #keycode_to_ascii
        bra .translate

.use_extended_table:
        ldx #keycode_to_ascii_extended
        bra .translate

.use_shifted_table:
        ldx #keycode_to_ascii_shifted

.translate:
        abx                     ; B contains the keycode, add it to the base
                                ; address in X
        lda 0,x
        bra .end

.handle_key_up_marker:
        oim #KEY_UP,flags       ; Set key up flag
        bra .end

.handle_shift:
        tim #KEY_UP,flags       ; flags and KEY_UP
        beq .shift_down         ; zero -> KEY_UP not set
.shift_up:
        aim #($ff-SHIFTED-KEY_UP),flags ; clear KEY_UP and SHIFTED
        bra .end
.shift_down:
        oim #SHIFTED,flags      ; set shifted flag
        bra .end

.handle_extended_marker:
        oim #EXTENDED,flags
        bra .end

.end:
        rts

        ENDSECTION ps2_decoder

        endif
