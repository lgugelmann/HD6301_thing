; Turn port 2 bit 4 on and off with a bit of delay in-between

        cpu 6301

PORT2_DDR = $0001
PORT2     = $0003

PORT2_BIT4 = %00010000
DELAY_LOOPS = 100

        org $e000
start:
        ;; Initialize stack pointer
        lds     #$0200

        ;; Set port 2 bit 4 to output
        lda #PORT2_BIT4
        sta PORT2_DDR

low:   
        ;; Set output low
        lda #$00
        sta PORT2

        ldb #$00
delay_low: 
        ;; waste some time
        inc B
        bne delay_low

        inc A
        cmp A,#DELAY_LOOPS
        beq high
        
        ldb #$00
        bra delay_low

high:   
        ;; Set output high
        lda #PORT2_BIT4
        sta PORT2

        ldb #$00
delay_high: 
        ;; waste some time
        inc B
        bne delay_high

        inc A
        cmp A,#DELAY_LOOPS
        beq low
        
        ldb #$00
        bra delay_high

        org $fff0
vectors:
        adr start               ; SCI
        adr start               ; Timer overflow
        adr start               ; Timer output compare
        adr start               ; Timer input capture
        adr start               ; IRQ1
        adr start               ; Software interrupt SWI
        adr start               ; NMI
        adr start               ; Reset / illegal address or instruction
