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

CLK  = %00000001
MOSI = %00000010
MISO = %10000000
CS   = %00000100

; General notes:
; - spi_send/receive_byte leave X untouched as we might have to do loops of 512
;   bytes. This avoids expensive pshx/pulx in those loops.
; - spi_discard_byte, spi_send_byte_ff/00, spi_end_command leave A untouched as
;   it's often carrying the value of an earlier spi_receive_byte that we want to
;   preserve.
; - MISO in bit 7 is an important optimization as it can be read without using a
;   register via 'tst IO_IRA' and the N flag. The rest of the SPI lines can be
;   placed anywhere.
; - Some code is already attempting to play nice to other users of port A, most
;   isn't.
; - There are definitely opportunities to optimize further. Some loops could be
;   unrolled if performance is paramount too.

spi_init:
        lda #CS
        ; CS high, CLK low, MOSI low (but that doesn't matter)
        sta IO_ORA
        lda #(CLK | MOSI | CS)
        sta IO_DDRA
        rts

; Start a SPI command. Leaves all registers intact, but clears the for non-SPI
; bits on port A too.
spi_start_command:
        ; Set CS low, CLK low (and assume we're the only ones on port A)
        clr IO_ORA
        rts

; End a SPI command. Clobbers B.
spi_end_command:
        ldb #CS
        stb IO_ORA
        rts

; Receives a byte and returns it in A. Clobbers B.
; Cycles: 8 + 8*19 (worst case) + 11 (jsr,rts) = 171
spi_receive_byte:
        ldb #MOSI
        stb IO_ORA

getbit  macro
        inc b                   ; CLK is bit 0
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we don't need the 'inc a'
        inc a
+
        dec b
        stb IO_ORA              ; Lower clock
        endm

        getbit                  ; bit 0
        getbit                  ; bit 1
        getbit                  ; bit 2
        getbit                  ; bit 3
        getbit                  ; bit 4
        getbit                  ; bit 5
        getbit                  ; bit 6
        getbit                  ; bit 7

        rts

; Sends the byte in A, clobbers A and B.
spi_send_byte:
        pshx
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
        pulx
        rts

; Send the ff byte. This is faster than spi_send_byte. This works corectly with
; CS both high or low. Assumes that CLK is low at the start. Clobbers B, X.
spi_send_byte_ff:
; Sending ff or discarding a byte are equivalent
spi_discard_byte:
        ldb IO_IRA              ; Make sure to get CS from the current state
        ora b,#MOSI

        ldx #8
.loop:
        inc b                   ; clock up
        stb IO_ORA
        dec b                   ; clock down
        stb IO_ORA
        dex
        bne .loop
        rts

; Send the 00 byte. This is faster than spi_send_byte. This works corectly with
; CS both high or low. Assumes that CLK is low at the start. Clobbers B, X.
spi_send_byte_00:
        ldb IO_IRA              ; Make sure to get CS from the current state
        and b,#~MOSI

        ldx #8
.loop:
        inc b                   ; clock up
        stb IO_ORA
        dec b                   ; clock down
        stb IO_ORA
        dex
        bne .loop
        rts

        ENDSECTION spi

        endif
