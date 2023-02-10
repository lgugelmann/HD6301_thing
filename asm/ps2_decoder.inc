        ifndef __ps2_decoder_inc
__ps2_decoder_inc = 1

        cpu 6301

KEY_UP   = %10000000            ; The next scancode indicates a key-up
SHIFTED  = %01000000            ; Shift is being held
EXTENDED = %00000100            ; Scancode is a control code from extended set
CONTROL  = %00000010            ; Scancode is a control code
ASCII    = %00000001            ; Scancode is ascii printable and translated

        include keyboard_keymap_us
        include macros

        zp_var wr_ptr,2
        zp_var rd_ptr,2
        zp_var flags,1

prev_org set *
        org $0400
keyboard_buffer: db.b 255

        org prev_org
ps2_decoder_init:
        clr wr_ptr
        clr rd_ptr
        clr flags
        rts

ps2_decoder_interrupt:  
        cmp b,#$f0              ; key up marker
        beq handle_key_up_marker
        cmp b,#$12              ; left shift
        beq handle_shift
        cmp b,#$59              ; right shift
        beq handle_shift
        cmp b,#$e0              ; extended character code marker
        beq handle_extended_marker

        tim #EXTENDED,flags
        bne handle_extended_flag ; previous byte was extended marker

        tim #KEY_UP,flags
        beq .not_key_up

        eim #KEY_UP,flags       ; clear KEY_UP
        bra irq_handled

.not_key_up:
        tim #SHIFTED,flags
        bne .shifted
        lda #(keycode_to_ascii / 256)
        bra .translate

.shifted:
        lda #(keycode_to_ascii_shifted / 256)
.translate:
        xgdx                    ; A contains the page address of the table, B
                                ; the index.
        lda 0,x
        jsr send_byte
        bra irq_handled

handle_key_up_marker:
        oim #KEY_UP,flags
        bra irq_handled

handle_shift:
        tim #KEY_UP,flags       ; flags and KEY_UP
        beq .shift_down         ; zero -> KEY_UP not set
.shift_up:
        aim #($ff-SHIFTED-KEY_UP),flags ; clear KEY_UP and SHIFTED
        bra irq_handled
.shift_down:
        oim #SHIFTED,flags
        bra irq_handled

handle_extended_marker:
        oim #EXTENDED,flags
        bra irq_handled

handle_extended_flag:
        eim #EXTENDED,flags     ; clear EXTENDED
        tim #KEY_UP,flags
        beq irq_handled         ; not KEY_UP

        eim #KEY_UP,flags       ; clear KEY_UP
        bra irq_handled

irq_handled:
        rts

        endif
