        cpu 6301

        include ../stdlib.inc

        SECTION graphics_test
        PUBLIC graphics_test_start

graphics_test_start:
        lda #1
        sta GRAPHICS_HIDE_CURSOR
        jsr init_strings
        jsr fg_color_test
        jsr bg_color_test
        jsr both_color_test
        rts

; Write out a few lines of 'abcd..' text
init_strings:
        ldb #0
.rowloop:
        stb GRAPHICS_SET_ROW
        clr GRAPHICS_SET_COLUMN
        lda #'0'
.charloop:
        sta GRAPHICS_SEND_CHAR
        inc a
        cmp a,#'z'+1
        bne .charloop
        inc b
        cmp b,#4
        bne .rowloop
        rts

fg_color_test:
        ldb #1
        stb GRAPHICS_SET_ROW
        clr GRAPHICS_SET_COLUMN
        clr a
.loop:
        sta GRAPHICS_SET_COLOR
        stb GRAPHICS_MOVE_CURSOR
        inc a
        cmp a,#64
        bne .loop
        rts

bg_color_test:
        ldb #2
        stb GRAPHICS_SET_ROW
        clr GRAPHICS_SET_COLUMN
        ldb #1
        lda #$40
.loop:
        sta GRAPHICS_SET_COLOR
        stb GRAPHICS_MOVE_CURSOR
        inc a
        cmp a,#$80
        bne .loop
        rts

both_color_test:
        ldb #3
        stb GRAPHICS_SET_ROW
        clr GRAPHICS_SET_COLUMN
        ldb #1
        lda #$80
.loop:
        sta GRAPHICS_SET_COLOR
        stb GRAPHICS_MOVE_CURSOR
        inc a
        cmp a,#$ff
        bne .loop
        rts

        ENDSECTION
