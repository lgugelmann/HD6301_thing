        ifndef __ps2_decoder_inc
__ps2_decoder_inc = 1

        cpu 6301

        ;; Constants for the ps2 decoding
KEY_UP   = %10000000            ; The next scancode indicates a key-up
SHIFTED  = %01000000            ; Shift is being held
EXTENDED = %00000100            ; Scancode is a control code from extended set

        ;; Flags stored in the keyboard buffer
CONTROL  = %00000010            ; Scancode is an untranslated control code
ASCII    = %00000001            ; Scancode is ascii printable and translated

        include keyboard_keymap_us
        include macros

        ;; Keyboard buffer pointers. wr_ptr points to the next memory location
        ;; to write to, rd_ptr to the next one to read from. If wr_ptr == rd_ptr
        ;; there is nothing more to read.
        zp_var wr_ptr,1
        zp_var rd_ptr,1
        zp_var buf_flags,1
        zp_var flags,1

prev_org set *
        org $0400
keyboard_buffer: ds.b 256

        org prev_org
ps2_decoder_init:
        clr DDR1                ; Set port 1 to input

        ;; Port 2 is connected to the keyboard controller interrupts
        ;; P20 is IRQ clear (on low), P21 indicates whether an interrupt
        ;; is a keyboard interrupt and is low on KBD interrupt
        lda #$01
        sta DDR2                ; Set port 2 to out/in/in
        sta PORT2               ; Set P20 to 1

        clr wr_ptr
        clr rd_ptr
        clr buf_flags
        clr flags
        rts

;; Returns in A the next character from the keyboard and in B the corresponding
;; flags. Blocks if none available. Clobbers X.
ps2_decoder_get_key:
        ldb rd_ptr
        cmp b,wr_ptr

        beq ps2_decoder_get_key

        ldx #keyboard_buffer
        abx

        ;; Get the character
        lda 0,x

        ;; Set B to 2 past the current read pointer
        inc b
        inc b
        stb rd_ptr

        ;; keyboard_buffer is page-aligned and we always read bytes two at a
        ;; time. INX can't overflow the page boundary here.
        inx
        ldb 0,x

        rts

ps2_decoder_interrupt:
        ;; Check port 2 to see if it's a keyboard interrupt
        lda PORT2
        bit A,#$02
        beq kbd_irq
        rts

kbd_irq:
        ldb PORT1               ; load keycode from port1
        ;; Send 0 then 1 on P20 to clear Keyboard interrupt
        lda #$01
        clr PORT2
        sta PORT2

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
        ;; Put (A,B) into X. A ist he base address of the table, B the index.
        lda #(keycode_to_flags / 256)
        xgdx
        lda 0,x
        sta buf_flags
        xgdx                    ; Swap X back, as we need B again

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

        ;; Set X to point to the next storage location in the keyboard buffer
        ;; and store the translated ASCII character
        ldb wr_ptr
        ldx #keyboard_buffer
        abx
        sta 0,x

        ;; Next store the flags. Note that we always store 2 bytes and the
        ;; keyboard buffer is page-aligned. The inx below never overflows the
        ;; page boundary.
        inx
        lda buf_flags
        sta 0,x

        ;; update wr_ptr
        inc b
        inc b
        stb wr_ptr

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
