; Reads data from a ps2 keyboard and prints it out via serial interface and on
; the graphical terminal

        cpu 6301

        org $e000

        include include/keyboard
        include include/registers
        include include/serial

start:
        lds #$7eff              ; Initialize the stack
        jsr keyboard_init
        jsr serial_init
        cli                     ; Enable interrupts

loop:
        jsr keyboard_getchar
        cmpa #0
        beq loop

        cmpa #$08              ; Backspace
        bne +
        ;; ASCII backspace just means 'move one back' on terminals. To implement
        ;; 'real' backspace we need to send backspace, space, backspace.
        sta TERMINAL_SEND_CHAR
        jsr serial_send_byte
        lda #$20                ; ASCII space
        jsr serial_send_byte
        lda #$08                ; ASCII backspace
        jsr serial_send_byte
        bra loop

+
        cmp A,#$0A              ; LF
        bne +
        lda #$0D                ; CR
        jsr serial_send_byte
        lda #$0A                ; LF

+
        sta TERMINAL_SEND_CHAR
        jsr serial_send_byte
        bra loop


irq:
        jsr keyboard_irq
        rti

        org $fff0
vectors:
        adr start               ; SCI
        adr start               ; Timer overflow
        adr start               ; Timer output compare
        adr start               ; Timer input capture
        adr irq                 ; IRQ1
        adr start               ; Software interrupt SWI
        adr start               ; NMI
        adr start               ; Reset / illegal address or instruction
