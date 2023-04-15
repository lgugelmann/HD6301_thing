        ifndef __keyboard_inc
__keyboard_inc = 1

        include macros
        include ps2_decoder
        include registers

        SECTION keyboard
        PUBLIC keyboard_init
        PUBLIC keyboard_irq
        PUBLIC keyboard_getchar

        zp_var rd_ptr,1
        zp_var wr_ptr,1

        ;; Reserve 256 bytes of keyboard buffer
        reserve_page keyboard_buffer

keyboard_init:
        clr DDR1                ; Set port 1 (keyboard data) to input

        ; Port 2 is connected to the keyboard controller interrupts P20 is IRQ
        ; clear (on low), P21 indicates whether an interrupt is a keyboard
        ; interrupt and is low on KBD interrupt. We set it to out/in/in.
        lda #$01
        ;
        sta DDR2
        sta PORT2               ; Set P20 to 1

        clr wr_ptr
        clr rd_ptr
        jsr ps2_decoder_init
        rts

; Handle keyboard interrupts: push the data from the keyboard onto the keyboard
; buffer and return.
keyboard_irq:
        tim #$02,PORT2          ; P21 is 0 if a keyboard interrupt is active
        bne .no_keyboard_irq

        lda PORT1               ; load keycode from port1
        ; Toggle P20 to clear Keyboard interrupt
        ldb #$01
        clr PORT2
        stb PORT2

        ; Set up X to point to the next slot to write to in the keyboard buffer,
        ; store keycode there, then increase write pointer.
        ldx #keyboard_buffer
        ldb wr_ptr
        abx
        sta 0,x
        inc wr_ptr

        ; Convention: set carry bit coming out of an interrupt subroutine to
        ; indicate that we handled the interrupt.
        sec
.no_keyboard_irq:
        rts

; Return the next character from the keyboard in A, or 0 if there isn't any.
keyboard_getchar:
        lda #$00

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
        rts

        ENDSECTION keyboard

        endif
