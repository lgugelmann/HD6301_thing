        ifndef __debug_macros_inc
__debug_macros_inc = 1

debug_print macro arg
        psh a
        lda arg
        jsr graphics_putchar
        pul a
        endm

debug_print_a macro arg
        psh a
        jsr graphics_putchar
        pul a
        endm

debug_print_b macro arg
        psh a
        tba
        jsr graphics_putchar
        pul a
        endm

        endif
