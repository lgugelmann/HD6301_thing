        ifndef __io_inc
__io_inc = 1

        cpu 6301

        include registers

        SECTION io
        PUBLIC io_init
        PUBLIC io_irq
        PUBLIC_SHARED io_timer1_callback

        zp_var io_timer1_callback,2
        zp_var break_counter,1

IO_IRQ_CA2 = %00000001
IO_IRQ_CA1 = %00000010
IO_IRQ_SR  = %00000100
IO_IRQ_CB2 = %00001000
IO_IRQ_CB1 = %00010000
IO_IRQ_T2  = %00100000
IO_IRQ_T1  = %01000000
IO_IRQ_ANY = %10000000

IO_PORTA_OE_MASK = %00000100

io_init:
        ; Set up port A to drive the the SD/Keyboard select bit
        clr IO_ORA              ; OE low, i.e. SD card drives Port A
        lda #IO_PORTA_OE_MASK
        sta IO_DDRA

        ; Port B is always inputs
        clr IO_DDRB
        ; Disable all interrupts. Bit 7 is 0 for 'clear where 1 is set'
        lda #$7f
        sta IO_IER
        ; Set the ACR & PCR to a known state
        clr IO_ACR
        clr IO_PCR
        ; Enable CA1 edge interrupts, that's what the keyboard drives
        lda #(IO_IRQ_CA1 | IO_IRQ_ANY)
        sta IO_IER
        rts

io_irq:
        lda IO_IFR
        bpl .not_our_irq        ; bit 7 is not set, not our irq

        ; Timer 1 interrupt.
        bit a,#IO_IRQ_T1
        beq +
        ; Clear interrupt bit
        lda #IO_IRQ_T1          ; Top bit unset -> clear
        sta IO_IFR
        ; Call the timer1 callback if provided.
        ldx io_timer1_callback
        beq .irq_handled
        jsr 0,x
        bra .irq_handled
+
        ; CA1 interrupt: Keyboard key pressed
        bit a,#IO_IRQ_CA1
        beq +

        ; Clear interrupt bit
        lda #IO_IRQ_CA1         ; Top bit unset -> clear
        sta IO_IFR

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

        ; Set the keyboard to output on port B
        lda IO_ORA
        ora a,#IO_PORTA_OE_MASK
        sta IO_ORA
        ; We're in interrupt context, nobody gets to change ORA under our
        ; feet. We keep A untouched so we don't have to relaod ORA again.

        ; Set X to point to the next write location in the keyboard buffer
        ldx #keyboard_buffer
        ldb wr_ptr
        abx
        ; Get the next byte from the keyboard port
        ldb IO_IRB
        ; Set port B back to read from the SD card. Assumes that A was untouched
        ; since the earlier IO_ORA read.
        and a,#~IO_PORTA_OE_MASK
        sta IO_ORA

        ; Is it the start of a break sequence?
        cmp b,#$e1
        beq .break_pressed
        ; If not, store the byte into the keyboard buffer
        stb 0,x
        inc wr_ptr
        clv                     ; No brek key pressed
        bra .irq_handled
.break_pressed:
        ldb #$08
        stb break_counter
.break_handler:
        dec break_counter       ; Also clears V for valid break_counter values
        bne .irq_handled
        sev                     ; Signal that we received the last byte of the
                                ; break sequence.
        bra .irq_handled
+
        ; Clear interrupt flags we're not handling
        lda #(IO_IRQ_CA1 | IO_IRQ_SR | IO_IRQ_CB2 | IO_IRQ_CB1 | IO_IRQ_T2)
        sta IO_IFR
.irq_handled:
        sec                     ; Indicate we handled the IRQ
.not_our_irq:
        rts


        ENDSECTION io

        endif
