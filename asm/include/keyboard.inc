        ifndef __keyboard_inc
__keyboard_inc = 1

        include macros
        include ps2_decoder
        include registers

        SECTION keyboard
        PUBLIC keyboard_init
        PUBLIC keyboard_getchar
        PUBLIC wr_ptr
        PUBLIC keyboard_buffer

        zp_var rd_ptr,1
        zp_var wr_ptr,1

        ;; Reserve 256 bytes of keyboard buffer
        reserve_page keyboard_buffer

keyboard_init:
        clr wr_ptr
        clr rd_ptr
        jsr ps2_decoder_init
        rts

; Return the next character from the keyboard in A, or 0 if there isn't any. Z
; is set / cleared depending on the state of A.
keyboard_getchar:
        clr a

        pshb
.decode_next:
        ; Compare keyboard pointers, data is available if they differ
        ldb rd_ptr
        cmp b,wr_ptr
        beq .end

        pshx
        ; Set up X as (keyboard_buffer + read_pointer), read the keyboard input
        ; into B, increment the read pointer.
        ldx #keyboard_buffer
        abx
        ldb 0,x
        inc rd_ptr
        jsr ps2_decoder_decode

        ; If we got a 0 back attempt to immediately decode some more
        cmp a,#$00
        pulx
        beq .decode_next
.end:
        pulb
        tst a
        rts

        ENDSECTION keyboard

        endif
