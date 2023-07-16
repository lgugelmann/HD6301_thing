        cpu 6301

        include ../stdlib.inc

        SECTION graphics_test
        PUBLIC graphics_test_start

graphics_test_start:
        clr GRAPHICS_CLEAR
        nop10
        nop10
        lda #1
        sta GRAPHICS_HIDE_CURSOR
        jsr init_strings
        jsr fg_color_test
        jsr bg_color_test
        jsr advance_color_test
        jsr print_color_reference
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
        cmp b,#5
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

advance_color_test:
        ldb #3
        stb GRAPHICS_SET_ROW
        clr GRAPHICS_SET_COLUMN
        lda #COLOR_ADVANCE
.loop:
        sta GRAPHICS_SET_COLOR
        inc a
        cmp a,#$40 | COLOR_ADVANCE
        bne .loop
        rts

print_color_reference:
        lda #KEY_ENTER
        jsr putchar             ; After this the cursor is on a new row

        ldb #0
        ldx #16
.loop:
        tba
        jsr putchar_hex

        lda #-2
        sta GRAPHICS_MOVE_CURSOR
        eor b,#COLOR_BG | COLOR_ADVANCE
        stb GRAPHICS_SET_COLOR
        stb GRAPHICS_SET_COLOR
        stb GRAPHICS_SET_COLOR
        eor b,#COLOR_BG | COLOR_ADVANCE

        inc b
        dex
        bne .loop
        lda #KEY_ENTER
        jsr putchar

        ldx #16
        cmp b,#$40
        bne .loop

        rts

        ENDSECTION
