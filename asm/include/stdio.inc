        ifndef __stdio_inc
__stdio_inc = 1

        cpu 6301

        include graphics
        include serial
        include keyboard

        SECTION stdio
        PUBLIC stdio_init
        PUBLIC getchar
        PUBLIC putchar
        PUBLIC putstring

stdio_init:
        jsr graphics_init
        jsr keyboard_init
        jsr serial_init
        rts

getchar:
        jsr keyboard_getchar
        rts

putchar:
        jsr serial_send_byte

        cmp a,#KEY_ENTER         ; LF, need to send a CR for serial
        bne .no_lf

        lda #$0d                ; CR
        jsr serial_send_byte
        lda #KEY_ENTER
.no_lf:
        jsr graphics_putchar
        rts

putstring:
        psh a
.loop:
        lda 0,x
        beq .end

        jsr putchar
        inx
        bra .loop
.end:
        pul a
        rts

        ENDSECTION stdio

        endif
