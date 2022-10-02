; Send "Hello, World!" endlessly over the SCI transmit line

        cpu 6301

PORT2_DDR = $0001
PORT2     = $0003

TRMCR = $0010                   ; Transfer Rate / Mode Control Register
TRCSR = $0011                   ; Transmit / Receive Control Status Register
TDR   = $0013                   ; Transmit Data Register

TRMCR_CC0  = %00000100          ; CC0 bit (low bit of clock source settings)

TRCSR_TE   = %00000010          ; Transmit Enable
TRCSR_TDRE = %00100000          ; Transmit Data Register Empty


        org $e000
hello:
        byt "Hello, World!\x00"

start:
        lds #$0200
        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE           ; Enable transmission
        sta TRCSR

send_hello:
        ldx #hello
hello_loop:     
        lda $00,X
        beq send_hello          ; Found the 0 byte - start from the top
        inx
        jsr send_byte
        bra hello_loop

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

