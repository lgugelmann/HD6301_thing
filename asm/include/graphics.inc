        ifndef __graphics_inc
__graphics_inc = 1

        include key_constants
        include macros
        include registers

GRAPHICS_TERMINAL_WIDTH  = 100
GRAPHICS_TERMINAL_HEIGHT = 40

        SECTION graphics
        PUBLIC graphics_init
        PUBLIC graphics_putchar

graphics_init:
        clr GRAPHICS_CLEAR
        clr GRAPHICS_HIDE_CURSOR
        rts

; Put the character in A on the screen. If the character is printable, print it
; and advance the cursor. For non-printable characters delete, move cursor
; etc. as appropriate. Clobbers A.
graphics_putchar:
        cmp a,#KEY_DELETE
        bne +
        lda #-1
        sta GRAPHICS_MOVE_CURSOR
        lda #' '
        sta GRAPHICS_SEND_CHAR_RAW
        bra .end
+
        cmp a,#KEY_ENTER
        bne +
        lda #2
        sta GRAPHICS_CLEAR
        bra .end
+
        bit a,#$80              ; Ignore control characters >127
        bne .end
        sta GRAPHICS_SEND_CHAR
.end
        rts

        ENDSECTION graphics

        endif
