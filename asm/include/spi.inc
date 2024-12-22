        ifndef __spi_inc
__spi_inc = 1

        cpu 6301

        include registers

        SECTION spi
        PUBLIC spi_init
        PUBLIC spi_start_command
        PUBLIC spi_end_command
        PUBLIC spi_receive_byte
        PUBLIC spi_discard_byte
        PUBLIC spi_send_byte
        PUBLIC spi_send_byte_ff
        PUBLIC spi_send_byte_00

CA2_LOW  = %00001100
CA2_HIGH = %00001110
CA2_BITS = ~%00001110

SR_IFR_FLAG = %00000100

; General notes:
; - spi_send/receive_byte leave X untouched as we might have to do loops of 512
;   bytes. This avoids expensive pshx/pulx in those loops.
; - spi_discard_byte, spi_send_byte_ff/00, spi_end_command leave A untouched as
;   it's often carrying the value of an earlier spi_receive_byte that we want to
;   preserve.

; Initialize SPI. We have to:
; - Set the shift register up for phi2-clocked output
; - Set port B as inputs for the input data
; - Set CA2 high as /CS line
spi_init:
        ; Shift register setup.
        lda #%00011000          ; Output, phi2-clocked
        sta IO_ACR

        ; Port B as inputs
        clr IO_DDRB

        ; CA2 initially high
        lda IO_PCR
        ora a,#CA2_HIGH
        sta IO_PCR
        rts

; Start a SPI command. Clobbers A.
spi_start_command:
        lda IO_PCR
        and a,#CA2_BITS
        ora a,#CA2_LOW
        sta IO_PCR
        rts

; End a SPI command. Clobbers B.
spi_end_command:
.wait:
        ldb IO_IFR
        and b,#SR_IFR_FLAG
        beq .wait

        ldb IO_PCR
        ora b,#CA2_HIGH         ; high is all 1, so or is sufficient
        stb IO_PCR
        rts

; Receives a byte and returns it in A.
spi_receive_byte:
        lda #$ff                ; We could skip this and send a random value
        sta IO_SR
        ; It takes 18 cycles for the data to show up at port A fully. This means
        ; we're most likely going to use 27 cycles here (3 loops, unless
        ; interrupted). TODO: optimize this so we get the data faster.
.wait:
        lda IO_IFR
        and a,#SR_IFR_FLAG
        beq .wait

        lda IO_IRB
        rts

; Sends the byte in A.
spi_send_byte:
        psh a
        pul a
        sta IO_SR
        rts

; Send the ff byte without touching A. Clobbers B.
spi_send_byte_ff:
; Sending ff or discarding a byte are equivalent
spi_discard_byte:
        psh b
        pul b                   ; Waste a bit of time. TODO: fix this.
        ldb #$ff
        stb IO_SR
        rts

; Send the 00 byte. This gets the SR clock going without touching any registers.
spi_send_byte_00:
        psh a
        pul a                   ; Waste a bit of time - we seem to be going too fast
        clr IO_SR
        rts

        ENDSECTION spi

        endif
