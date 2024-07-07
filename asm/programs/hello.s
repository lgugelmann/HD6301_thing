        cpu 6301

        include ../stdlib.inc

        SECTION hello_program
        PUBLIC hello_start

hello_string:
        byt "Hello \0"
world_string:
        byt "World!\0"

hello_start:
        clr GRAPHICS_CLEAR
        ldx #hello_string
        jsr putstring
        ; Check if a parameter was passed in. If so, print it after Hello.
        tsx
        ldx 2,x                 ; Get the parameter string
        lda 0,x                 ; First char of the string
        bne .print
        ldx #world_string
.print:
        jsr putstring
.loop:
        jsr getchar
        beq .loop
        rts

        ENDSECTION
