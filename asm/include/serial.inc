        ifndef __serial_inc
__serial_inc = 1

        cpu 6301

        include registers

        SECTION serial
        PUBLIC serial_init
        PUBLIC serial_irq
        PUBLIC_SHARED serial_send_byte
        PUBLIC_SHARED serial_send_byte_dec
        PUBLIC_SHARED serial_send_string
        PUBLIC_SHARED serial_clear_screen
        PUBLIC_SHARED serial_read_byte
        PUBLIC_SHARED serial_read_byte_blocking

        ;; Reserve 256 bytes of serial input buffer
        reserve_page serial_in_buffer
        ;; Pointers into the above buffer
        zp_var rd_ptr,1         ; Where to read next
        zp_var wr_ptr,1         ; Where to write next

;; Initialize Serial Communication Interface
serial_init:
        clr wr_ptr
        clr rd_ptr

        lda #TRMCR_CC0
        sta TRMCR               ; Set clock source to internal and rate to E/16

        ;; Enable receive + interrupts / transmit
        lda #TRCSR_TE | TRCSR_RE | TRCSR_RIE
        sta TRCSR
        rts

;; Handle SCI read interrupts. Pushes the new data onto the serial receive
;; buffer and advances the relevant pointers. These are the internal serial
;; interrupts which have their own vector.
serial_irq:
        lda TRCSR
        bit A,#TRCSR_RDRF       ; receive register full -> nonzero result here
        bne .got_byte
        clc
        rts
.got_byte:
        lda RDR
        ldx #serial_in_buffer
        ldb wr_ptr
        abx
        sta 0,x
        inc b
        stb wr_ptr
        rti

;; Read the next character on the serial line into A, non-blocking.
serial_read_byte
        clr a
        psh b
        ldb rd_ptr
        cmp b,wr_ptr
        beq _serial_read_buffer_end

_serial_read_buffer:
        pshx
        ldx #serial_in_buffer
        abx
        lda 0,x
        inc b
        stb rd_ptr
        pulx
_serial_read_buffer_end:
        pul b
        rts

;; Read the next character on the serial line into A, blocking.
serial_read_byte_blocking:
        psh b
.read_loop:
        ldb rd_ptr
        cmp b,wr_ptr
        beq .read_loop
        bra _serial_read_buffer

;; Send the byte in A over serial
serial_send_byte:
        ;; Wait for the send queue to be empty
.wait_empty:
        tim #TRCSR_TDRE,TRCSR
        beq .wait_empty

        sta TDR
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

serial_clear_screen:
        pshx
        ldx #.serial_clear_screen_command
        jsr serial_send_string
        pulx
        rts
.serial_clear_screen_command:
        byt "\x1b[2J\0"

serial_send_byte_dec:
        psh a
        psh b
        tab

        lda #"0" - 1
.hundred_loop:
        inc a
        sub b,#100
        bcc .hundred_loop
        jsr serial_send_byte    ; Write hundreds digit

        lda #"9" + 1
.tens_loop:
        dec a
        add b,#10
        bmi .tens_loop
        jsr serial_send_byte    ; Write tens digit

        add b,#"0"
        tba
        jsr serial_send_byte    ; Write last digit

        pul b
        pul a
        rts


        ENDSECTION

        endif
