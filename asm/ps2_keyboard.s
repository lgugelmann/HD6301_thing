; Reads data from a ps2 keyboard and prints it out via serial interface

        cpu 6301

        org $e000

        include include/registers
        include include/ps2_decoder

start:
        lds #$0200              ; Initialize the stack
        jsr ps2_decoder_init

        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE|TRCSR_RE  ; Enable read / transmit
        sta TRCSR

        cli                     ; Enable interrupts

loop:
        jsr ps2_decoder_get_key
        cmp B,#$01
        bne .control_code
        jsr send_byte
        bra loop                ; Sit around waiting for IRQs

.control_code:
        cmp A,#$66              ; Backspace
        bne +
        ;; ASCII backspace just means 'move one back' on terminals. To implement
        ;; 'real' backspace we need to send backspace, space, backspace.
        lda #$08                ; ASCII backspace
        jsr send_byte
        lda #$20                ; ASCII space
        jsr send_byte
        lda #$08                ; ASCII backspace
        jsr send_byte

+
        cmp A,#$5A              ; Enter
        bne +
        lda #$0D                ; CR
        jsr send_byte
        lda #$0A                ; LF
        jsr send_byte

+
        bra loop

        ;; Send byte stored in A
send_byte:
        ;; Wait for the send queue to be empty
.wait_empty:
        ldb TRCSR
        bit B,#TRCSR_TDRE
        beq .wait_empty

        sta TDR
        rts


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
