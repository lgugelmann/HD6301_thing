        cpu 6301

        include ../stdlib.inc
        include ../include/rtc

        SECTION clock_program
        PUBLIC clock_start

clock_start:
        jsr i2c_init
        jsr rtc_init
.loop:
        jsr getchar
        beq .loop               ; no key yet
        cmp a,#"q"
        beq .quit
        cmp a,#"t"
        bne .loop
        jsr rtc_get_time
        ; Print HH:MM:SS (BCD values display correctly via putchar_hex)
        lda rtc_hours
        jsr putchar_hex
        lda #":"
        jsr putchar
        lda rtc_minutes
        jsr putchar_hex
        lda #":"
        jsr putchar
        lda rtc_seconds
        jsr putchar_hex
        jsr put_newline
        bra .loop
.quit:
        rts

        ENDSECTION
