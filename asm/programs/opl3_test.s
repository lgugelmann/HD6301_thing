        cpu 6301

        include ../stdlib.inc

        SECTION opl3_test
        PUBLIC opl3_test_start

opl3_test_start:
        ; Enable OPL3 mode
        lda #$05
        sta OPL3_ADDRESS_ARRAY1
        lda #$01
        sta OPL3_DATA_WRITE

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
        beq .read_loop

        cmp a,#'a'
        bne +

        ldb #$b0
        stb OPL3_ADDRESS_ARRAY0
        ldb #$2e                ; Play note
        stb OPL3_DATA_WRITE
        bra .read_loop
+
        cmp a,#'s'
        bne +

        ldb #$b0
        stb OPL3_ADDRESS_ARRAY0
        ldb #$00                ; Stop note
        stb OPL3_DATA_WRITE
        bra .read_loop
+
        ; Enable timer 1
        cmp a,#'t'
        bne +

        ldb #OPL3_TIMER1
        stb OPL3_ADDRESS_ARRAY0
        ldb #$00
        stb OPL3_DATA_WRITE

        ldd #timer1_callback
        std sound_timer1_callback
        jsr enable_timer1
        bra .read_loop
+
        ; Disable timer 1
        cmp a,#'T'
        bne +

        jsr disable_timer
        bra .read_loop
+
        ; Enable timer 2
        cmp a,#'r'
        bne +

        ldb #OPL3_TIMER2
        stb OPL3_ADDRESS_ARRAY0
        ldb #$00
        stb OPL3_DATA_WRITE

        ldd #timer2_callback
        std sound_timer2_callback
        jsr enable_timer2
        bra .read_loop
+
        ; Disable timer 2
        cmp a,#'R'
        bne +

        jsr disable_timer
        bra .read_loop
+
        cmp a,#'x'
        bne +
        rts                     ; Quit program, return to monitor
+
        bra .read_loop

enable_timer1:
        sei                     ; mask interrupts to avoid register ops
                                ; interfering
        lda #OPL3_TIMER_CONTROL
        sta OPL3_ADDRESS_ARRAY0
        lda #$01
        sta OPL3_DATA_WRITE
        cli
        rts

enable_timer2:
        sei                     ; mask interrupts to avoid register ops
                                ; interfering
        lda #OPL3_TIMER_CONTROL
        sta OPL3_ADDRESS_ARRAY0
        lda #$02
        sta OPL3_DATA_WRITE
        cli
        rts

disable_timer:
        sei                     ; mask interrupts to avoid register ops
                                ; interfering
        lda #OPL3_TIMER_CONTROL
        sta OPL3_ADDRESS_ARRAY0
        lda #$00
        sta OPL3_DATA_WRITE
        cli
        rts

timer1_callback:
        lda #'1'
        jsr putchar
        rts

timer2_callback:
        lda #'2'
        jsr putchar
        rts

        ENDSECTION
