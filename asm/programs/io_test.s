        cpu 6301

        include ../stdlib.inc
        include ../include/spi.inc

        SECTION io_test
        PUBLIC io_test_start

        zp_var sd_address, 4
        reserve_memory sd_block_buffer, 512

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
        jsr spi_send_byte_ff
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
        beq .cmd8_success
        jmp .failed_initialization
.cmd8_success:

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

        clr sd_address
        clr sd_address + 1
        clr sd_address + 2
        lda #$01
        sta sd_address + 3

        jsr sd_read_block
        tst a
        bne .error
        jsr test_print_sd_block

        lda #'e'
        jsr putchar
        lda #KEY_ENTER
        jsr putchar

        clr sd_address + 3
        jsr sd_read_block
        tst a
        bne .error
        jsr test_print_sd_block

        lda #$01
        sta sd_address + 3
        jsr sd_write_block
        tst a
        bne .error

        jsr sd_read_block
        tst a
        bne .error
        jsr test_print_sd_block

        bra .end

.error:
        psh a
        ldx #.error_string
        jsr putstring
        pul a
        jsr putchar_hex
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

.error_string:
        byt "Error: \0"

.success_string:
        byt "SD card initialized successfully\n\0"

.sdhc_string:
        byt "SDHC card: \0"

test_print_sd_block:
        ldx #sd_block_buffer
.loop:
        lda 0,x
        jsr putchar_hex
        inx
        cpx #sd_block_buffer+512
        bne .loop
        lda #KEY_ENTER
        jsr putchar
        rts

sd_get_r1:
        ldx #10
.loop:
        jsr spi_receive_byte
        tst a
        bpl .end                ; top bit is clear: R1
        dex
        bne .loop
.end:
        rts

sd_get_start_block:
        ldx #100
.loop:
        jsr spi_receive_byte
        cmp a,#$ff
        bne .end
        dex
        bne .loop
.end:
        rts

sd_get_data_response:
        ldx #10
.loop:
        jsr spi_receive_byte
        cmp a,#$ff
        bne .end
        dex
        bne .loop
.end
        rts

; Loop for as long as we get all 0
sd_wait_busy:
        jsr spi_receive_byte
        tst a
        beq sd_wait_busy
        rts

; Send CMD17 to read a single block with address sd_address. Returns 0 on
; success, R1 or a data error token on failure. The data error token has the top
; bit set to 1 to distinguish it from R1.
sd_read_block:
        jsr spi_start_command
        lda #$40 + 17
        jsr spi_send_byte

        lda sd_address
        jsr spi_send_byte
        lda sd_address + 1
        jsr spi_send_byte
        lda sd_address + 2
        jsr spi_send_byte
        lda sd_address + 3
        jsr spi_send_byte

        ; CRC, ignored
        jsr spi_send_byte_ff

        jsr sd_get_r1
        tst a
        bne .end

        jsr sd_get_start_block
        ; If we got an error token instead of a block start, the top bit is 0
        bmi .no_error
        ora a,#$80              ; Set the top bit to distinguish it from R1
        bra .end

.no_error:
        ldx #sd_block_buffer
.loop:
        jsr spi_receive_byte
        sta 0,x
        inx
        cpx #sd_block_buffer + 512
        bne .loop

        ; 16-bit CRC
        jsr spi_discard_byte
        jsr spi_discard_byte

        clr a                   ; Clear A to indicate no error
.end:
        ; These both leave A untouched
        jsr spi_end_command
        jsr spi_send_byte_ff
        rts

sd_write_block:
        jsr spi_start_command
        lda #$40 + 24
        jsr spi_send_byte

        lda sd_address
        jsr spi_send_byte
        lda sd_address + 1
        jsr spi_send_byte
        lda sd_address + 2
        jsr spi_send_byte
        lda sd_address + 3
        jsr spi_send_byte

        ; CRC, ignored
        jsr spi_discard_byte

        jsr sd_get_r1
        tst a
        bne .end

        lda #$fe                ; start block token
        jsr spi_send_byte

        ldx #sd_block_buffer
.loop:
        lda 0,x
        jsr spi_send_byte
        inx
        cpx #sd_block_buffer + 512
        bne .loop

        ; 16-bit CRC, not checked
        jsr spi_discard_byte
        jsr spi_discard_byte

        jsr sd_get_data_response
        tab
        and a,#%00001010        ; If any of these are set we have an error
        beq .no_error
        ; We need to distinguish between R1 and the response here. R1 is
        ; guaranteed to have a 0 in the top bit, so we put a 1 here. The data
        ; response token has bit 7 unspecified.
        tba
        ora a,#$80
        psh a
        jsr sd_wait_busy
        pul a
        bra .end

.no_error:
        jsr sd_wait_busy
        clr a
.end:
        jsr spi_end_command
        jsr spi_send_byte_ff
        rts

; GO_IDLE_STATE, response R1
sd_send_cmd0:
        jsr spi_start_command
        lda #$40
        jsr spi_send_byte

        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00

        lda #$95
        jsr spi_send_byte

        jsr sd_get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff
        rts

; SEND_IF_COND, R7
sd_send_cmd8:
        jsr spi_start_command
        lda #$48
        jsr spi_send_byte

        ; Payload
        jsr spi_send_byte_00
        jsr spi_send_byte_00
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
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte

        jsr spi_end_command

        jsr spi_send_byte_ff

        pul a
        rts

; SEND_CID
sd_send_cmd10:
        jsr spi_start_command
        lda #$40 + 10
        jsr spi_send_byte

        ; Payload (don't care)
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        ; CRC (don't care)
        jsr spi_send_byte_00

        jsr sd_get_r1
        bne .end                ; if non-zero we have an error
        jsr putchar_hex
        lda #' '
        jsr putchar

        jsr sd_get_start_block
        bpl .end

        ; CID is sent as a data token. 0xfe start block, 16 register bytes, 16
        ; bit CRC.
        ldx #18
.loop:
        jsr spi_receive_byte
        jsr putchar_hex
        dex
        bne .loop

        lda #KEY_ENTER
        jsr putchar
.end:
        jsr spi_end_command
        jsr spi_send_byte_ff
        rts

; Send CMD55 (APP_CMD). Clobbers B, X, returns R1 in A.
sd_send_cmd55:
        jsr spi_start_command
        lda #$77
        jsr spi_send_byte
        ; 4 random bytes (command doesn't care)
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
	; CRC (not checked)
        jsr spi_send_byte_00

        jsr sd_get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff

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
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        ; CRC (not checked)
        jsr spi_send_byte_00

        jsr sd_get_r1

        jsr spi_receive_byte

        ; None of the commands below touch A
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_end_command
        jsr spi_send_byte_ff

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
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
	; CRC (not checked)
        jsr spi_send_byte_00

        jsr sd_get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff

        rts

        ENDSECTION
