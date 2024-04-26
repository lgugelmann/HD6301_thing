        cpu 6301

        include ../stdlib.inc

        SECTION io_test
        PUBLIC io_test_start

io_test_start:
        lda #$ff
        sta IO_DDRA
        sta IO_ORA

.loop:
        jsr delay_500ms
        clr IO_ORA
        jsr delay_500ms
        sta IO_ORA
        bra .loop

        ENDSECTION
