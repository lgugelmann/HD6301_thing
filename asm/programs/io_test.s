        cpu 6301

        include ../stdlib.inc

        SECTION io_test
        PUBLIC io_test_start

; - CLK is PA0
; - MOSI is PA1
; - MISO is PA7
; - CS is PA2

CLK  = %00000001
MOSI = %00000010
MISO = %10000000
CS   = %00000100

READ_COMMAND = %00000011

io_test_start:
        jsr spi_init
.loop:
        jsr getchar
        beq .loop

        cmp a,#'r'
        bne .loop
        jsr spi_read
        lda #KEY_ENTER
        jsr putchar
        bra .loop

        rts

spi_init:
        lda #(CLK | MOSI | CS)
        sta IO_DDRA
        lda #CS
        ; /CS high, CLK low, MOSI low (but that doesn't matter)
        sta IO_ORA
        rts

spi_read:
        ; Set /CS low, CLK low (and assume we're the only ones on port A)
        clr IO_ORA

        lda #READ_COMMAND
        jsr spi_send_byte
        clr A
        jsr spi_send_byte
        clr A
        jsr spi_send_byte

        ldb #32
.loop:
        psh b
        jsr spi_read_byte
        jsr putchar_hex
        lda #' '
        jsr putchar
        pul b
        dec b
        bne .loop

        ldb #CS
        stb IO_ORA

        rts

; Sends the byte in A, clobbers A, B, and X
spi_send_byte:
        ldx #8
.loop:
        asl a
        bcc .send_zero
        ldb #MOSI
        stb IO_ORA
        ldb #(MOSI | CLK)
        stb IO_ORA
        clr IO_ORA
        dex
        bne .loop
        bra .end
.send_zero:
        ; clr IO_ORA    ; can assume this is already done
        ldb #CLK
        stb IO_ORA
        clr IO_ORA
        dex
        bne .loop
.end:
        rts

; Read a byte and returns it in A. Clobbers B, X.
spi_read_byte:
        ldx #8
        ldb #CLK
.loop:
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl .zero_in            ; On 0 we're done
        inc a
.zero_in:
        clr IO_ORA              ; Lower clock
        dex
        bne .loop

        rts

; Blink something on PA0 on and off once a second
blink:
        lda #$ff
        sta IO_DDRA
        sta IO_ORA

.loop:
        jsr delay_500ms
        clr IO_ORA
        jsr delay_500ms
        sta IO_ORA
        bra .loop

        ENDSECTION
