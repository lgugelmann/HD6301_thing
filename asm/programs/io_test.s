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

        zp_var sd_read_address, 4

io_test_start:
        jsr spi_init
.loop:
        jsr getchar
        beq .loop

        cmp a,#'r'
        bne .loop
        jsr sd_init
        bra .loop

        rts

; R1 response format
; - 0abcdefg with
;   - a: parameter error
;   - b: address error
;   - c: erase sequences error
;   - d: command CRC error
;   - e: illegal command
;   - f: erase reset
;   - g: in idle state

; SD Card initialization
; - Send CMD0: $40 $00 $00 $00 $00 $95
;   - Expected response: R1 with $01 (busy)
; - Send CMD8: $48 $00 $00 $01 $AA $xx (CRC ignored)
;   - Expected response: 5 bytes, R1 + 4 answer bytes
l; - (optional) CMD48 (read OCR)
; - ACMD41
;   - Send CMD55 (argument: 4 bytes, don't care) to indicate next is app
;     - Response: R1
;   - Send ACMD41 (HCS=0): $69 $00 $00 $00 $00 $xx
;     - Response: R1
; - Repeat ACMD41 until R1 idle state is cleared
; - CMD58 (read OCR): $7a $xx $xx $xx $xx $xx
;   - Response R3:
;   - CCS bit is OCR[30] (1 == HC card, 512 bytes sector size fixed)
; - Done!

sd_init:
        ; Need to provide >74 clock pulses while CS is high
        lda #10
.clk_loop:
        jsr sd_send_extra_clock
        dec a
        bne .clk_loop

        ; Send CMD0, attempt it 5 times
        lda #5
.cmd0_loop:
        psh a
        jsr sd_send_cmd0
        cmp a,#1
        beq .cmd0_success
        pul a
        dec a
        bne .cmd0_loop
        jmp .failed_initialization

.cmd0_success:
        pul a                   ; Undo the 'psh a' above

        ; Send CMD8
        jsr sd_send_cmd8
        cmp a,#1
        bne .failed_initialization

        ; Send ACMD41, wait until status is 0
.acmd41:
        ; Spec says that we need to poll at <50ms delay
        jsr delay_10ms
        jsr sd_send_acmd41
        tst a
        bne .acmd41

        ldx #.success_string
        jsr putstring

        ldx #.sdhc_string
        jsr putstring
        jsr sd_send_cmd58
        and a,#$40
        beq +
        lda #'1'
        jsr putchar
        bra ++
+
        lda #'0'
        jsr putchar
+
        lda #KEY_ENTER
        jsr putchar

        jsr sd_send_cmd10

        lda #'r'
        jsr putchar

        clr sd_read_address
        clr sd_read_address + 1
        clr sd_read_address + 2
        lda #$00
        sta sd_read_address + 3

        jsr sd_read_block
        jsr putchar_hex

        lda #'e'
        jsr putchar
        lda #KEY_ENTER
        jsr putchar

        bra .end

.failed_initialization:
        ldx #.failed_string
        jsr putstring
.end:
        rts

.failed_string:
        byt "Failed to initialize SD card\n\0"

.success_string:
        byt "SD card initialized successfully\n\0"

.sdhc_string:
        byt "SDHC card: \0"


; Send 8 clock pulses while CS is high. This is required by the spec after every
; command ends. Clobbers B, X.
sd_send_extra_clock:
        ldx #8
.loop:
        ldb #(CS | CLK | MOSI)
        stb IO_ORA
        ldb #(CS | MOSI)
        stb IO_ORA
        dex
        bne .loop

        rts

sd_get_r1:
        ldx 10
.loop:
        jsr spi_read_byte
        tst a
        bpl .end                ; top bit is clear: R1
        dex
        bne .loop
.end:
        rts

sd_get_start_block:
        ldx 10
.loop:
        jsr spi_read_byte
        cmp a,#$ff
        bne .end
        dex
        bne .loop
.end:
        rts

; Send CMD17 to read a single block with address sd_read_address
sd_read_block:
        jsr spi_start_command
        lda #$40 + 17
        jsr spi_send_byte

        lda sd_read_address
        jsr spi_send_byte
        lda sd_read_address + 1
        jsr spi_send_byte
        lda sd_read_address + 2
        jsr spi_send_byte
        lda sd_read_address + 3
        jsr spi_send_byte

        jsr sd_get_r1
        tst a
        psh a
        bne .end

        jsr sd_get_start_block
        ; If we got an error token instead of a block start the top bit is 0
        bpl .end

        ldx #512
.loop:
        jsr spi_read_byte
        jsr putchar_hex
        dex
        bne .loop

        ; 16-bit CRC
        jsr spi_read_byte
        jsr spi_read_byte

.end:
        jsr spi_end_command
        jsr sd_send_extra_clock
        pul a
        rts


; GO_IDLE_STATE, response R1
sd_send_cmd0:
        jsr spi_start_command
        lda #$40
        jsr spi_send_byte

        lda #$00
        jsr spi_send_byte
        lda #$00
        jsr spi_send_byte
        lda #$00
        jsr spi_send_byte
        lda #$00
        jsr spi_send_byte

        lda #$95
        jsr spi_send_byte

        jsr sd_get_r1

        jsr spi_end_command

        jsr sd_send_extra_clock
        rts

; SEND_IF_COND, R7
sd_send_cmd8:
        jsr spi_start_command
        lda #$48
        jsr spi_send_byte

        ; Payload
        lda #$00
        jsr spi_send_byte
        lda #$00
        jsr spi_send_byte
        lda #$01
        jsr spi_send_byte
        lda #$AA
        jsr spi_send_byte

        ; CRC (checked!)
        lda #$87
        jsr spi_send_byte

        ; 5 bytes of response, R1 + 4 bytes
        jsr sd_get_r1
        psh a
        jsr spi_read_byte
        jsr spi_read_byte
        jsr spi_read_byte
        jsr spi_read_byte

        jsr spi_end_command

        jsr sd_send_extra_clock

        pul a
        rts

; SEND_CID
sd_send_cmd10:
        jsr spi_start_command
        lda #$40 + 10
        jsr spi_send_byte

        ; Payload (don't care)
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
        ; CRC (don't care)
        jsr spi_send_byte

        jsr sd_get_r1
        bne .end                ; if non-zero we have an error
        jsr putchar_hex
        lda #' '
        jsr putchar

        jsr sd_get_start_block
        bpl .end

        ; CID is sent as a data token. 0xfe start block, 16 register bytes, 16
        ; bit CRC.
        ldb #18
.loop:
        psh b
        jsr spi_read_byte
        jsr putchar_hex
        pul b
        dec b
        bne .loop

        lda #KEY_ENTER
        jsr putchar
.end:
        jsr spi_end_command
        jsr sd_send_extra_clock
        rts

; Send CMD55 (APP_CMD). Clobbers B, X, returns R1 in A.
sd_send_cmd55:
        jsr spi_start_command
        lda #$77
        jsr spi_send_byte
        ; 4 random bytes (command doesn't care)
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
	; CRC (not checked)
        jsr spi_send_byte

        jsr sd_get_r1

        jsr spi_end_command

        jsr sd_send_extra_clock

        rts

; Send CMD58 (READ_OCR). Clobbers B, X. Returns the top OCR byte in A.
sd_send_cmd58
        ; Send CMD58 (read OCR): $7a $xx $xx $xx $xx $xx
        ;   - Response R3: R1 + 32bit OCR
        ;   - CCS bit is OCR[30] (1 == HC card, 512 bytes sector size fixed)
        jsr spi_start_command
        lda #$7a
        jsr spi_send_byte
        ; 4x don't care
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
        ; CRC (not checked)
        jsr spi_send_byte

        jsr sd_get_r1

        jsr spi_read_byte
        psh a
        jsr spi_read_byte
        jsr spi_read_byte
        jsr spi_read_byte

        jsr spi_end_command

        jsr sd_send_extra_clock

        pul a

        rts

; SD_SEND_OP_COND
sd_send_acmd41:
        jsr sd_send_cmd55

        jsr spi_start_command
        lda #$69
        jsr spi_send_byte
        lda #$40
        jsr spi_send_byte
        ; 3 more random bytes
        jsr spi_send_byte
        jsr spi_send_byte
        jsr spi_send_byte
	; CRC (not checked)
        jsr spi_send_byte

        jsr sd_get_r1

        jsr spi_end_command

        jsr sd_send_extra_clock

        rts


; Set port A up for SPI communication
spi_init:
        lda #CS
        ; /CS high, CLK low, MOSI low (but that doesn't matter)
        sta IO_ORA
        lda #(CLK | MOSI | CS)
        sta IO_DDRA
        rts

; Start a SPI command
spi_start_command:
        ; Set /CS low, CLK low (and assume we're the only ones on port A)
        clr IO_ORA
        rts

; End a SPI command. Clobbers B.
spi_end_command:
        ldb #CS
        stb IO_ORA
        rts

; Sends the byte in A, clobbers A, B, and X.
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

; Read a byte and returns it in A. Clobbers B.
spi_read_byte:
        ; Unrolled 8 times to avoid having to use X or psh/pul
        ldb #(MOSI)
        stb IO_ORA

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        eor b,#CLK
        stb IO_ORA              ; Raise clock
        asl a
        tst IO_IRA              ; Sets N to PA7
        bpl +                   ; On 0 we're done
        inc a
+
        eor b,#CLK
        stb IO_ORA              ; Lower clock

        rts

        ENDSECTION
