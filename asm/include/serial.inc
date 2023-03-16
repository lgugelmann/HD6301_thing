        ifndef __serial_inc
__serial_inc = 1

        cpu 6301

        include registers

;; Initialize Serial Communication Interface
serial_init:
        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        lda #TRCSR_TE|TRCSR_RE  ; Enable read / transmit
        sta TRCSR

        rts

;; Send the byte in A over serial
serial_send_byte:
        psh b
        ;; Wait for the send queue to be empty
.wait_empty:
        ldb TRCSR
        bit B,#TRCSR_TDRE
        beq .wait_empty

        sta TDR
        pul b
        rts


        endif
