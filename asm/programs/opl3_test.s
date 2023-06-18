        cpu 6301

        include ../stdlib.inc

        SECTION opl3_test
        PUBLIC opl3_test_start

opl3_test_start:
        lda #$20
        sta OPL3_ADDRESS_ARRAY0
        lda #$21
        sta OPL3_DATA_WRITE

        lda #$40
        sta OPL3_ADDRESS_ARRAY0
        lda #$8f
        sta OPL3_DATA_WRITE

        lda #$60
        sta OPL3_ADDRESS_ARRAY0
        lda #$f2
        sta OPL3_DATA_WRITE

        lda #$80
        sta OPL3_ADDRESS_ARRAY0
        lda #$45
        sta OPL3_DATA_WRITE

        lda #$23
        sta OPL3_ADDRESS_ARRAY0
        lda #$21
        sta OPL3_DATA_WRITE

        lda #$43
        sta OPL3_ADDRESS_ARRAY0
        lda #$00
        sta OPL3_DATA_WRITE

        lda #$63
        sta OPL3_ADDRESS_ARRAY0
        lda #$f2
        sta OPL3_DATA_WRITE

        lda #$83
        sta OPL3_ADDRESS_ARRAY0
        lda #$76
        sta OPL3_DATA_WRITE

        lda #$a0
        sta OPL3_ADDRESS_ARRAY0
        lda #$ae
        sta OPL3_DATA_WRITE

        lda #$b0
        sta OPL3_ADDRESS_ARRAY0
        lda #$00
        sta OPL3_DATA_WRITE

.read_loop:
        jsr getchar
        cmp a,#'a'
        bne +

        ; Array is already set up to $b0
        ldb #$2e                ; Play note
        stb OPL3_DATA_WRITE
        bra .read_loop

+
        cmp a,#'s'
        bne +

        ; Array is already set up to $b0
        ldb #$00                ; Stop note
        stb OPL3_DATA_WRITE
        bra .read_loop

+
        cmp a,#'x'
        bne +
        rts                     ; Quit program, return to monitor

+
        bra .read_loop

        ENDSECTION
