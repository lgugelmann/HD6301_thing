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
        PUBLIC_SHARED put_newline
        PUBLIC_SHARED putstring
        PUBLIC_SHARED putx_dec
        PUBLIC_SHARED putx_hex

stdio_init:
        jsr graphics_init
        jsr keyboard_init
        jsr serial_init
        rts

; Input one character from the keyboard in A. Returns 0 if no character is
; available.
getchar:
        jsr serial_read_byte
        beq .read_keyboard
        cmp a,#13
        bne .end
        lda #KEY_ENTER
        rts
.read_keyboard:
        jsr keyboard_getchar
.end:
        rts


; Output the character in A on both graphical and serial terminal
putchar:
        jsr serial_send_byte
        ; If that was a backspace, also clear the deleted character.
        cmp a,#KEY_BACKSPACE
        bne .no_backspace

        lda #' '
        jsr serial_send_byte
        lda #KEY_BACKSPACE
        jsr serial_send_byte
        jmp .serial_handled
.no_backspace:
        cmp a,#KEY_ENTER        ; LF, need to send a CR for serial
        bne .serial_handled

        lda #$0d                ; CR
        jsr serial_send_byte
        lda #KEY_ENTER
.serial_handled:
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
        bls .end
        add a,#("A" - "0" - 10)
.end:
        jsr putchar
        pul a
        rts

; Outputs a KEY_ENTER character
put_newline:
        psh a
        lda #KEY_ENTER
        jsr putchar
        pul a
        rts

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

; Output the two bytes in X as 5 decimal digits, 0-padded
putx_dec:
        pshx
        psh a
        psh b

        lda #"0" - 1
.10k_loop:
        inc a
        xgdx
        subd #10000
        xgdx
        bcc .10k_loop
        jsr putchar

        lda #"9" + 1
.1k_loop:
        dec a
        xgdx
        addd #1000
        xgdx
        bmi .1k_loop
        jsr putchar

        lda #"0" - 1
.100_loop:
        inc a
        xgdx
        subd #100
        xgdx
        bcc .100_loop
        jsr putchar

        lda #"9" + 1
.10_loop:
        dec a
        xgdx
        addd #10
        xgdx
        bmi .10_loop
        jsr putchar             ; Write tens digit

        xgdx
        add b,#"0"
        tba
        jsr putchar             ; Write last digit

        pul b
        pul a
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
