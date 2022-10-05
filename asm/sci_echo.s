; Echo whatever comes in from the serial console

        cpu 6301

TRMCR = $0010                   ; Transfer Rate / Mode Control Register
TRCSR = $0011                   ; Transmit / Receive Control Status Register
RDR   = $0012                   ; Receive Data Register
TDR   = $0013                   ; Transmit Data Register


TRMCR_CC0  = %00000100          ; CC0 bit (low bit of clock source settings)

TRCSR_TE   = %00000010          ; Transmit Enable
TRCSR_RE   = %00001000          ; Read Enable
TRCSR_TDRE = %00100000          ; Transmit Data Register Empty
TRCSR_RDRF = %10000000          ; Receive Data Register Full


        org $e000
start:
        lds #$0200              ; Initialize the stack
        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE|TRCSR_RE  ; Enable read / transmit
        sta TRCSR

wait_read:
        lda TRCSR
        bit A,#TRCSR_RDRF       ; receive register full -> nonzero result here
        beq wait_read
        lda RDR
        jsr send_byte
        bra wait_read

        ;; Send byte stored in A 
send_byte:      
        ;; Wait for the send queue to be empty
.wait_empty:
        ldb TRCSR
        bit B,#TRCSR_TDRE
        beq .wait_empty

        sta TDR
        rts

vectors:
        org $fffe
        adr start

