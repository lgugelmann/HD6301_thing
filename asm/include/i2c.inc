        ifndef __i2c_inc
__i2c_inc = 1

        cpu 6301

        include registers

        SECTION i2c
        PUBLIC i2c_init
        PUBLIC_SHARED i2c_send_byte

; A simple bit-banging i2c implementation. Assumes no clock stretching or bus
; sharing. The idea is to write a 0 into the output register for the SCL/SDA
; pins, and then work with the corresponding DDR bit to assert a 0 (DDR output)
; or let the line float to 1 (DDR input). We also assume that the other bits in
; DDRA stay constant while the code runs - i.e. any IRQ routine that touches
; them must leave them in the same state on exit as it found them.

I2C_SCL = %00000001
I2C_SDA = %00000010

; Initializes the i2c module. Clobbers A, X.
i2c_init:
        ; Make sure SCL/SDA output a 0 when set as outputs. Using aim makes for
        ; an (overkill) atomic read/modify/write.
        ldx #IO_ORA
        aim #~(I2C_SCL | I2C_SDA),0,x

        ; Set them both as inputs so they float up in the idle state
        ldx #IO_DDRA
        aim #~(I2C_SCL | I2C_SDA),0,x
        rts

; Sets SCL to a known idle state. Leaves B with the current state of DDRA.
i2c_scl_idle macro
        ldb IO_DDRA
        and b,#~I2C_SCL
        stb IO_DDRA
        endm

; Sets SCL low. Assumes the current state of DDRA is in B.
i2c_scl_low macro
        ora b,#I2C_SCL
        stb IO_DDRA
        endm

; Sets SCL high. Assumes the current state of DDRA is in B.
i2c_scl_high macro
        and b,#~I2C_SCL
        stb IO_DDRA
        endm

; Sets SDA low. Assumes the current state of DDRA is in B.
i2c_sda_low macro
        ora b,#I2C_SDA
        stb IO_DDRA
        endm

; Sets SDA high. Assumes the current state of DDRA is in B.
i2c_sda_high macro
        and b,#~I2C_SDA
        stb IO_DDRA
        endm

; Send the i2c start bit. Leaves B with the current DDRA state.
i2c_start:
	; Reset to known-good state.
        i2c_scl_idle
        ; Start transfer, SCL high, SDA low
        i2c_sda_high
        i2c_sda_low
        rts

; Send the stop bit.
i2c_stop:
        i2c_scl_low
        i2c_sda_low
        i2c_scl_high
        i2c_sda_high
        rts

; Sends the byte in A, returns the ACK bit in A and the zero flag. Assumes SCL
; is high on entry and that B contains the DDRA state. Leaves SCL high on exit,
; and B with the post-transfer DDRA state. Clobbers X.
i2c_write_byte:
        ldx #8
.bit_loop:
        i2c_scl_low
        asl a
        bcc .bit0
.bit1:
        i2c_sda_high
        bra .next
.bit0:
        i2c_sda_low
.next:
        i2c_scl_high
        dex
        bne .bit_loop

	; Read ACK bit
        i2c_scl_low
        i2c_sda_high
        i2c_scl_high
        lda IO_IRA
        and a,#I2C_SDA
        rts

; Sends the byte in B to the address in A. Clobbers B, X. On return A contains
; the first ACK bit with an error - or the last one on success.
i2c_send_byte:
        psh b
        jsr i2c_start
        ; Address byte is A6|..|A0|W for write, with W=0
        asl a
        jsr i2c_write_byte
        pul b                   ; leaves flags untouched
        bne .ack_error
        tba
        ldb IO_DDRA             ; B needs to be DDRA for i2c_*
        jsr i2c_write_byte
.ack_error:
        jmp i2c_stop
        ; rts in i2c_stop

        ENDSECTION i2c

        endif
