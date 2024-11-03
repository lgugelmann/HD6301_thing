        cpu 6301

        include ../stdlib.inc

        SECTION w65c22_timer_program
        PUBLIC w65c22_timer_start

IFR_TIMER1 = %01000000

w65c22_timer_start:
        clr GRAPHICS_CLEAR

.loop:
        jsr getchar
        beq .loop

        lda #$ff
        sta IO_T1C_L
        sta IO_T1C_H

.timeout_wait:
        lda IO_IFR
        and A,#IFR_TIMER1
        bne .loop

        lda #'.'
        jsr putchar
        bra .timeout_wait

        rts

        ENDSECTION
