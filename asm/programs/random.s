        cpu 6301

        include ../stdlib.inc

        SECTION random_program
        PUBLIC random_start

random_start:
        ldx #16
.loop:
        ldb #16
.row:
        jsr random_byte
        jsr putchar_dec
        lda #" "
        jsr putchar
        dec b
        bne .row
        lda #"\n"
        jsr putchar
        dex
        bne .loop
        rts

        ENDSECTION
