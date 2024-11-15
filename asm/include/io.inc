        ifndef __io_inc
__io_inc = 1

        cpu 6301

        include registers

        SECTION io
        PUBLIC io_init
        PUBLIC io_irq
        PUBLIC_SHARED io_timer1_callback

        zp_var io_timer1_callback,2

IO_IRQ_CA2 = %00000001
IO_IRQ_CA1 = %00000010
IO_IRQ_SR  = %00000100
IO_IRQ_CB2 = %00001000
IO_IRQ_CB1 = %00010000
IO_IRQ_T2  = %00100000
IO_IRQ_T1  = %01000000
IO_IRQ_ANY = %10000000

io_init:
        ; Set all IO ports to input
        clr IO_DDRA
        clr IO_DDRB
        ; Disable interrupts. Bit 7 is 0 for 'clear where 1 is set'
        lda #(~IO_IRQ_ANY & $ff)
        sta IO_IER
        rts

io_irq:
        lda IO_IFR
        bpl .end                ; bit 7 is not set, not our irq

        ; Handle timer 1 by calling the relevant callback if provided
        bit a,IO_IRQ_T1
        bne +
        ldx io_timer1_callback
        beq +
        jsr 0,x
+
        ; Clear all interrupt flags
        lda #$7f
        sta IO_IFR
        sec
.end:
        rts


        ENDSECTION io

        endif
