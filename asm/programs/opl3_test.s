        cpu 6301

        include ../stdlib.inc

        SECTION opl3_test
        PUBLIC opl3_test_start

opl3_test_start:
        ; Set up a piano on channels 1..18
        lda #18
.loop:
        ldx #general_midi_instruments + 11*10
        jsr sound_load_instrument
        dec a
        bne .loop

        lda #4
        ldx #instruments_piano_nochannel
        jsr sound_load_instrument

        lda #5
        ldx #instruments_piano_left
        jsr sound_load_instrument

        lda #6
        ldx #instruments_piano_right
        jsr sound_load_instrument

        lda #7
        ldx #instruments_tsch
        jsr sound_load_instrument

        lda #8
        ldx #instruments_tunk
        jsr sound_load_instrument

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
        lda #13                 ; Channel
        ldb #69                 ; Note number
        jsr sound_play_note
        lda #14                 ; Channel
        ldb #72                 ; Note number
        jsr sound_play_note
        lda #15                 ; Channel
        ldb #76                 ; Note number
        jsr sound_play_note
        bra .read_loop
+
        cmp a,#'l'
        bne +
        lda #5
        ldb #69
        jsr sound_play_note
        jmp .read_loop
+
        cmp a,#'r'
        bne +
        lda #6
        ldb #69
        jsr sound_play_note
        jmp .read_loop
+
        cmp a,#'n'
        bne +
        lda #4
        ldb #69
        jsr sound_play_note
        jmp .read_loop
+
        cmp a,#'z'
        bne +

        ; Play a tsch sound
        lda #7                  ; Channel
        ldb #80                 ; Note number
        jsr sound_play_note
        jmp .read_loop
+
        cmp a,#'x'
        bne +

        ; Play a tunk sound
        lda #8                  ; Channel
        ldb #55                 ; Note number
        jsr sound_play_note
        jmp .read_loop
+
        cmp a,#'1'
        bne +
        ldb #0
        lsr b
        eor b,#63
        bra .set_volume
+
        cmp a,#'2'
        bne +
        ldb #31
        lsr b
        eor b,#63
        bra .set_volume
+
        cmp a,#'3'
        bne +
        ldb #63
        lsr b
        eor b,#63
        bra .set_volume
+
        cmp a,#'4'
        bne +
        ldb #95
        lsr b
        eor b,#63
        bra .set_volume
+
        cmp a,#'5'
        bne +
        ldb #127
        lsr b
        eor b,#63
        bra .set_volume

.set_volume:
        lda #3
        jsr sound_set_attenuation
        lda #2
        jsr sound_set_attenuation
        lda #1
        jsr sound_set_attenuation
        jmp .read_loop
+
        cmp a,#'s'
        bne +
        lda #18
.stop_loop:
        jsr sound_stop_note
        dec a
        bne .stop_loop
        jmp .read_loop
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
        jmp .read_loop
+
        ; Disable timer 1
        cmp a,#'T'
        bne +

        jsr disable_timer
        jmp .read_loop
+
        ; Enable timer 2
        cmp a,#'e'
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
        cmp a,#'E'
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
