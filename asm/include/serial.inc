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

;; Read the next character on the serial line into A, blocking.
serial_read_byte_blocking:
        lda TRCSR
        bit A,#TRCSR_RDRF       ; receive register full -> nonzero result here
        beq serial_read_byte_blocking
        lda RDR
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

;; Send the string pointed to by X. The string must be null terminated.
serial_send_string:
        psh a
.loop:
        lda 0,x
        beq .end

        jsr serial_send_byte
        inx
        bra .loop
.end:
        pul a
        rts

__serial_clear_screen_command:
        dc.B "\x1b[2J\0"

serial_clear_screen:
        pshx
        ldx #__serial_clear_screen_command
        jsr serial_send_string
        pulx
        rts

        endif
