        cpu 6301

        include ../stdlib.inc

        SECTION opl3_test
        PUBLIC opl3_test_start

opl3_test_start:
        ; Set up a piano on channels 1..9
        ldx #instruments_piano
        lda #9
.loop:
        jsr sound_load_instrument
        dec a
        bne .loop

.read_loop:
        jsr getchar
        beq .read_loop

        cmp a,#'a'
        bne +

        ; Play an A chord
        lda #1                  ; Channel
        ldb #69                 ; Note number
        jsr sound_play_note
        lda #2                  ; Channel
        ldb #73                 ; Note number
        jsr sound_play_note
        lda #3                  ; Channel
        ldb #76                 ; Note number
        jsr sound_play_note
        bra .read_loop
+
        cmp a,#'b'
        bne +

        ; Play an Am chord
        lda #4                  ; Channel
        ldb #69                 ; Note number
        jsr sound_play_note
        lda #5                  ; Channel
        ldb #72                 ; Note number
        jsr sound_play_note
        lda #6                  ; Channel
        ldb #76                 ; Note number
        jsr sound_play_note
        bra .read_loop
+
        cmp a,#'s'
        bne +

        lda #9
.stop_loop:
        jsr sound_stop_note
        dec a
        bne .stop_loop
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
        jmp .read_loop
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
        jmp .read_loop
+
        ; Disable timer 2
        cmp a,#'R'
        bne +

        jsr disable_timer
        jmp .read_loop
+
        cmp a,#'x'
        bne +
        rts                     ; Quit program, return to monitor
+
        jmp .read_loop

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
