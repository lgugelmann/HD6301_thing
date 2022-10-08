;
; Write out ROM contents over serial.
;
; At startup blocks until it receives one byte over serial (value doesn't
; matter). When it receives it, proceeds to dump the entire ROM contents over
; serial. There is a 'ROM dump OK' string in the first few ROM bytes that should
; help diagnose any serial decoding problems.
;

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

        byt "ROM dump OK"       ; Throw a few recognizable bytes at the start of
                                ; the ROM as a sanity check after dumping it.

start:
        lds #$0200
        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE|TRCSR_RE  ; Enable read / transmit
        sta TRCSR

wait_start:
        lda TRCSR
        bit A,#TRCSR_RDRF       ; receive register full -> nonzero result here
        beq wait_start
        lda RDR

        ldx #$E000              ; Dump the external ROM as well. Helps with
                                ; sanity checking that the serial comms are ok.
read_rom:
        lda $00,X
        jsr send_byte
        inx
        bne read_rom

end:
        bra end

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
