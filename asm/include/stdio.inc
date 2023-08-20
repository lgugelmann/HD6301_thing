        ifndef __stdio_inc
__stdio_inc = 1

        cpu 6301

        include graphics
        include macros
        include serial
        include keyboard

        SECTION stdio
        PUBLIC stdio_init
        PUBLIC_SHARED getchar
        PUBLIC_SHARED putchar
        PUBLIC_SHARED putchar_bin
        PUBLIC_SHARED putchar_dec
        PUBLIC_SHARED putchar_hex
        PUBLIC_SHARED putstring
        PUBLIC_SHARED putx_hex

stdio_init:
        jsr graphics_init
        jsr keyboard_init
        jsr serial_init
        rts

; Input one character from the keyboard in A. Returns 0 if no character is
; available.
getchar:
        jmp keyboard_getchar

; Output the character in A on both graphical and serial terminal
putchar:
        jsr serial_send_byte

        cmp a,#KEY_ENTER         ; LF, need to send a CR for serial
        bne .no_lf

        lda #$0d                ; CR
        jsr serial_send_byte
        lda #KEY_ENTER
.no_lf:
        jmp graphics_putchar
        ; rts in graphics_putchar

; Output the byte in A as a binary string, msb first
putchar_bin:
        psh b
        pshx
        tab
        ldx #9
.loop:
        dex
        beq .end
        ;; shift bits in B left into carry and print '0' or '1' as needed
        rol B
        bcs .print_one
        lda #"0"
        bra .print
.print_one:
        lda #"1"
.print:
        jsr putchar
        bra .loop

.end:
        pulx
        pul b
        rts

; Output the (unsigned) byte in A as 3 decimal digits.
putchar_dec:
        psh a
        psh b
        tab

        lda #"0" - 1
.hundred_loop:
        inc a
        sub b,#100
        bcc .hundred_loop
        jsr putchar             ; Write hundreds digit

        lda #"9" + 1
.tens_loop:
        dec a
        add b,#10
        bmi .tens_loop
        jsr putchar             ; Write tens digit

        add b,#"0"
        tba
        jsr putchar             ; Write last digit

        pul b
        pul a
        rts

; Output the byte in A as two hexadecimal characters
putchar_hex:
        psh a
        ; High nibble
        lsr a
        lsr a
        lsr a
        lsr a
        add a,#"0"
        cmp a,#"9"
        bls .number             ; If 0..9, print it
        add a,#("A" - "0" - 10) ; If >=10, move into the A..F range instead
.number:
        jsr putchar

        ; Low nibble
        pul a
        and a,#$0f
        add a,#"0"
        cmp a,#"9"
        bls putchar             ; rts in putchar
        add a,#("A" - "0" - 10)
        bra putchar             ; rts in putchar

; Output the two bytes in X as hexadecimal characters
putx_hex:
        pshx
        xgdx                    ; A, B stored in X
        jsr putchar_hex
        tba
        jsr putchar_hex
        xgdx                    ; restore A, B
        pulx
        rts

; Outputs the 0-terminated string pointed to by X. After running, X is
; guaranteed to point to the 0 byte at the end of the string.
putstring:
        psh a
.loop:
        lda 0,x
        beq .end

        jsr putchar
        inx
        bra .loop
.end:
        pul a
        rts

        ENDSECTION stdio

        endif
