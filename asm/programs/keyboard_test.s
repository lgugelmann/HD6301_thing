        cpu 6301

        include ../stdlib.inc

        SECTION keyboard_test_program
        PUBLIC keyboard_test_start

keyboard_test_string:
        byt "Keyboard test - hit any key to see its code\n\0"

keyboard_test_start:
        clr GRAPHICS_CLEAR
        ldx #keyboard_test_string
        jsr putstring
.loop:
        jsr getchar
        beq .loop
        jsr putchar_dec
        tab
        lda #' '
        jsr putchar
        tba
        jsr putchar_hex
        lda #KEY_ENTER
        jsr putchar
        bra .loop

        rts

        ENDSECTION
