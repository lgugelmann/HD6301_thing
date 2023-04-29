        cpu 6301

        include ../stdlib.inc

        SECTION hello_program
        PUBLIC hello_start

hello_string:
        byt "Hello World!\n\0"

hello_start:
        clr GRAPHICS_CLEAR
        ldx #hello_string
        jsr putstring
.loop:
        jsr getchar
        tst a
        beq .loop
        rts

        ENDSECTION
