        cpu 6301

        include ../stdlib.inc

        SECTION test_program
        PUBLIC test_start

test_string:
        byt "\nUser program start\n\0"

test_start:
        clr GRAPHICS_CLEAR      ; Clear screen
        ldx #test_string
        jsr putstring
        jsr test_user_program_function
        rts                     ; Back to the monitor

test_user_program_function:
        jsr getchar
        beq test_user_program_function
        jsr putchar

        cmp a,#"X"              ; X closes the program
        bne +
        rts
+
        cmp a,#"S"              ; S causes a software interrupt
        bne +
        swi
+
        bra test_user_program_function

        ENDSECTION
