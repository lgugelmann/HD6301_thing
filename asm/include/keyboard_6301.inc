        ifndef __keyboard_6301_inc
__keyboard_6301_inc = 1

        include macros
        include ps2_decoder
        include registers

        SECTION keyboard_6301
        PUBLIC keyboard_6301_init
        PUBLIC keyboard_6301_irq

        zp_var break_counter,1

keyboard_6301_init:
        clr DDR1                ; Set port 1 (keyboard data) to input

        ; Port 2 is connected to the keyboard controller interrupts P20 is IRQ
        ; clear (on low), P21 indicates whether an interrupt is a keyboard
        ; interrupt and is low on KBD interrupt. We set it to out/in/in.
        lda #$01
        ;
        sta DDR2
        sta PORT2               ; Set P20 to 1

        rts

; Handle keyboard interrupts: push the data from the keyboard onto the keyboard
; buffer (except for the 'break' key). Sets the carry bit (C) if a keyboard
; interrupt was indeed handled, and the overflow bit (V) if the 'break' key was
; pressed.
keyboard_6301_irq:
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

        ENDSECTION keyboard_6301

        endif
