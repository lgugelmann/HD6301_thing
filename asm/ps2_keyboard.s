; Reads data from a ps2 keyboard and prints it out via serial interface

        cpu 6301

        org $e000

        include registers
        include ps2_decoder

start:
        lds #$0200              ; Initialize the stack
        clr DDR1                ; Set port 1 to input

        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE|TRCSR_RE  ; Enable read / transmit
        sta TRCSR

        jsr ps2_decoder_init

        cli                     ; Enable interrupts

loop:
        bra loop                ; Sit around waiting for IRQs

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
        ldb PORT1               ; load keycode from port1
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
