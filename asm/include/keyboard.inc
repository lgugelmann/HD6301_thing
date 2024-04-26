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
        zp_var break_counter,1

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
        clr break_counter
        jsr ps2_decoder_init
        rts

; Handle keyboard interrupts: push the data from the keyboard onto the keyboard
; buffer (except for the 'break' key). Sets the carry bit (C) if a keyboard
; interrupt was indeed handled, and the overflow bit (V) if the 'break' key was
; pressed.
keyboard_irq:
        tim #$02,PORT2          ; P21 is 0 if a keyboard interrupt is active
        bne .end

        lda PORT1               ; load keycode from port1
        ; Toggle P20 to clear Keyboard interrupt
        ldb #$01
        clr PORT2
        stb PORT2

        ; Pause/Break has a special handler and is used to invoke the monitor
        ; program. Its sequence is $E1, $14, $77, $E1, $F0, $14, $F0, $77 and
        ; does not have a 'release' event. The byte $E1 is unique and not used
        ; in any other scancode. When we see it we know that the next 7 bytes
        ; are all part of that code. On receipt of the last byte we signal that
        ; the monitor is to be invoked. None of this makes it into the keyboard
        ; buffer.
        tst break_counter       ; Are we counting down a break sequence? Side
                                ; effect: clears V (which is why we don't clear
                                ; it earlier).
        bne .break_handler
        cmp a,#$e1
        beq .break_pressed

        ; Set up X to point to the next slot to write to in the keyboard buffer,
        ; store keycode there, then increase write pointer.
        ldx #keyboard_buffer
        ldb wr_ptr
        abx
        sta 0,x
        inc wr_ptr

        clv                     ; No break key pressed
        bra .handled_keyboard_irq

.break_pressed:
        lda #$08
        sta break_counter
.break_handler:
        dec break_counter       ; Also clears V for valid break_counter values
        bne .handled_keyboard_irq
        sev                     ; Signal that we received the last byte of the
                                ; break sequence.
.handled_keyboard_irq:
        sec
.end:
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
