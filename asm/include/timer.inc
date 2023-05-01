        ifndef __timer_inc
__timer_inc = 1

        cpu 6301

        include macros
        include registers

        SECTION timer
        PUBLIC timer_init
        PUBLIC timer_irq
        PUBLIC_SHARED timer_ticks

        zp_var timer_ticks,1

timer_init:
        clr timer_ticks
        ; Enable timer interrupts
        lda #TCSR_ETOI
        sta TCSR
        rts


timer_irq:
        ; Clear timer overflow flag by reading register + counter
        lda TCSR
        ldx TCH
        ldx #2^16-50000         ; We want a timer tick every ~50ms
        stx TCH

        inc timer_ticks
        rti

        ENDSECTION timer

        endif
