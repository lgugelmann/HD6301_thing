; Reads data from a ps2 keyboard and prints it out via serial interface

        cpu 6301

        org $e000

        include include/registers
        include include/ps2_decoder

start:
        lds #$0200              ; Initialize the stack
        clr DDR1                ; Set port 1 to input

        ;; Port 2 is connected to the keyboard controller interrupts
        ;; P20 is IRQ clear (on low), P21 indicates whether an interrupt
        ;; is a keyboard interrupt and is low on KBD interrupt
        lda #$01
        sta DDR2                ; Set port 2 to out/in/in
        sta PORT2               ; Set P20 to 1

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
        ;; Check if it's a keyboard interrupt
        lda PORT2
        bit A,#$02
        beq kbd_irq

        lda #$69
        jsr send_byte
        rti

kbd_irq:
        ldb PORT1               ; load keycode from port1
        ;; Send 0 then 1 on P20 to clear Keyboard interrupt
        lda #$01
        clr PORT2
        sta PORT2
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
