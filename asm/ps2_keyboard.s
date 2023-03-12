; Reads data from a ps2 keyboard and prints it out via serial interface

        cpu 6301

        org $e000

        include include/ps2_decoder
        include include/serial

start:
        lds #$0200              ; Initialize the stack
        jsr ps2_decoder_init
        jsr serial_init
        cli                     ; Enable interrupts

loop:
        jsr ps2_decoder_get_key
        cmp B,#$01
        bne .control_code
        jsr serial_send_byte
        bra loop                ; Sit around waiting for IRQs

.control_code:
        cmp A,#$66              ; Backspace
        bne +
        ;; ASCII backspace just means 'move one back' on terminals. To implement
        ;; 'real' backspace we need to send backspace, space, backspace.
        lda #$08                ; ASCII backspace
        jsr serial_send_byte
        lda #$20                ; ASCII space
        jsr serial_send_byte
        lda #$08                ; ASCII backspace
        jsr serial_send_byte

+
        cmp A,#$5A              ; Enter
        bne +
        lda #$0D                ; CR
        jsr serial_send_byte
        lda #$0A                ; LF
        jsr serial_send_byte

+
        bra loop


irq:
        jsr ps2_decoder_interrupt
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
