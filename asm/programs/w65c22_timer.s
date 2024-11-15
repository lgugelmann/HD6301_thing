        cpu 6301

        include ../stdlib.inc

        SECTION w65c22_timer_program
        PUBLIC w65c22_timer_start

        zp_var counter,1

IFR_TIMER1 = %01000000

w65c22_timer_start:
        clr GRAPHICS_CLEAR

.loop:
        jsr getchar
        beq .loop

        cmp a,#"p"
        bne .irq_test

.poll_test:
        lda #$ff
        sta IO_T1C_L
        sta IO_T1C_H

.poll_wait:
        lda IO_IFR
        and A,#IFR_TIMER1
        bne .loop

        lda #'.'
        jsr putchar
        bra .poll_wait

.irq_test:
        clr counter
        ldx #timer1_callback
        stx io_timer1_callback

        lda #$ff
        sta IO_T1C_L
        sta IO_T1C_H

        ; Top bit set --> set corresponding IRQ
        lda #IFR_TIMER1 | $80
        sta IO_IER

.irq_test_wait:
        lda counter
        bne +
        lda #'.'
        jsr putchar
        bra .irq_test_wait

+
        ; Disable timer1 interrupts
        lda #IFR_TIMER1
        sta IO_IER

        ldx #0
        stx io_timer1_callback

        bra .loop

        rts

timer1_callback:
        inc counter
        rts

        ENDSECTION
