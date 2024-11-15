; A benchmark to test how quickly we can read one page (512 bytes) off of the SD
; card.

        cpu 6301

        include ../stdlib.inc

        SECTION sdbench_program
        PUBLIC sdbench_start

        zp_var overflows,1
        reserve_memory buffer,512

sdbench_start:
        jsr getchar
        beq sdbench_start

        cmp a,#"q"
        bne +
        rts
+
        clr overflows
        ldx #buffer
        stx sd_card_io_buffer_address

        ; Make sure t1 interrupts are off before touching the callback
        lda #%01000000
        sta IO_IER

        ldx #timer1_callback
        stx io_timer1_callback

        clr sd_card_block_address
        clr sd_card_block_address+1
        clr sd_card_block_address+2
        clr sd_card_block_address+3

        ; Set timer1 to continuous mode
        lda IO_ACR
        ora #$40
        sta IO_ACR

	; Kick off the timer
        lda #$ff
        sta IO_T1C_L
        sta IO_T1C_H

        ; Enable timer1 interrupts
        lda #%11000000
        sta IO_IER

        jsr sd_card_read_block

	; Turn off timer1 interrupts
        lda #%01000000
        sta IO_IER

        ; Grab the current timer value
        ldb IO_T1C_L
        lda IO_T1C_H
        ; Turn it into elapsed time (timer counts down)
        ; ffff - D = ffff + (~A)(~B)+1 = (~A)(~B)
        com a
        com b
        xgdx
        jsr putx_dec
        lda #" "
        jsr putchar
        lda overflows
        jsr putchar_hex
        lda #KEY_ENTER
        jsr putchar

        clr overflows
        clr io_timer1_callback
        clr io_timer1_callback+1

        bra sdbench_start

        rts

timer1_callback:
        inc overflows
        rts

        ENDSECTION
