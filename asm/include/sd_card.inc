        ifndef __sd_card_inc
__sd_card_inc = 1

        cpu 6301

        include macros
        include spi

        SECTION sd_card

        PUBLIC_SHARED sd_card_block_address
        PUBLIC_SHARED sd_card_io_buffer_address

        PUBLIC sd_card_init
        PUBLIC_SHARED sd_card_initialize
        PUBLIC_SHARED sd_card_read_block
        PUBLIC_SHARED sd_card_write_block

        ; Address to read from / write to with the next block command. First
        ; byte is MSB, last is LSB.
        reserve_system_memory sd_card_block_address, 4
        ; The address at the start of 512 bytes to be written to the card, or
        ; the address to read 512 bytes into.
        reserve_system_memory sd_card_io_buffer_address, 2

        ; Holds the address of the end of the io buffer. Internal.
        zp_var io_buffer_end_address, 2

; Initializes the sd_card module
sd_card_init:
        jsr spi_init
        ldx #$0000
        stx sd_card_block_address
        stx sd_card_block_address + 2
        rts

; Performs SD card initialization. Returns zero in A on success, non-zero on
; failure. Clobbers A, B, X.
sd_card_initialize:
        ; Provide >74 clock pulses while CS is high
        lda #10
.clk_loop:
        jsr spi_send_byte_ff
        dec a
        bne .clk_loop

        ; Send CMD0
        jsr send_cmd0
        cmp a,#1
        beq .cmd0_success
        jmp .failed_initialization
.cmd0_success:
        ; Send CMD8
        jsr send_cmd8
        cmp a,#1
        beq .cmd8_success
        jmp .failed_initialization
.cmd8_success:
        ; Send ACMD41, wait until status is 0
.acmd41:
        ; Spec says that we need to poll at <50ms delay, and we need to wait for
        ; at least 1s before giving up (TODO: actually give up!)
        jsr delay_10ms
        jsr send_acmd41
        tst a
        bne .acmd41

        ; We only support SDHC cards for now. (Regular SD cards would just need
        ; a CMD16 to set block size to 512 - but I don't have any to test.)
        jsr send_cmd58
        and a,#%11000000
        cmp a,#%11000000
        bne .failed_initialization

        clr a                   ; Success!
        rts

.failed_initialization:
        ; TODO: some error strings describing the failure would be useful here
        lda #01
        rts

; Send CMD17 to read a single block with address sd_card_block_address. Returns
; 0 on success, R1 or a data error token on failure. The data error token has
; the top bit set to 1 to distinguish it from R1. The data is read into the 512
; bytes at and following the address in sd_card_io_buffer_address. Clobbers B,
; X.
sd_card_read_block:
        jsr spi_start_command
        lda #$40 + 17
        jsr spi_send_byte

        lda sd_card_block_address
        jsr spi_send_byte
        lda sd_card_block_address + 1
        jsr spi_send_byte
        lda sd_card_block_address + 2
        jsr spi_send_byte
        lda sd_card_block_address + 3
        jsr spi_send_byte

        ; CRC, ignored
        jsr spi_send_byte_ff

        jsr get_r1
        tst a
        bne .end

        jsr get_start_block
        ; If we got an error token instead of a block start, the top bit is 0
        bmi .no_error
        ora a,#$80              ; Set the top bit to distinguish it from R1
        bra .end

.no_error:
        ldx sd_card_io_buffer_address
        jsr spi_receive_block

        ; 16-bit CRC
        jsr spi_discard_byte
        jsr spi_discard_byte

        clr a                   ; Clear A to indicate no error
.end:
        ; These both leave A untouched
        jsr spi_end_command
        jsr spi_send_byte_ff
        rts

sd_card_write_block:
        jsr spi_start_command
        lda #$40 + 24
        jsr spi_send_byte

        lda sd_card_block_address
        jsr spi_send_byte
        lda sd_card_block_address + 1
        jsr spi_send_byte
        lda sd_card_block_address + 2
        jsr spi_send_byte
        lda sd_card_block_address + 3
        jsr spi_send_byte

        ; CRC, ignored
        jsr spi_discard_byte

        jsr get_r1
        tst a
        bne .end

        lda #$fe                ; start block token
        jsr spi_send_byte

        ldx sd_card_io_buffer_address
        ; Add 512 to the address above
        xgdx
        add a,#2
        xgdx
        stx io_buffer_end_address
        ldx sd_card_io_buffer_address
.loop:
        lda 0,x
        jsr spi_send_byte
        inx
        cpx io_buffer_end_address
        bne .loop

        ; 16-bit CRC, not checked
        jsr spi_discard_byte
        jsr spi_discard_byte

        jsr get_data_response
        tab
        and a,#%00001010        ; If any of these are set we have an error
        beq .no_error
        ; We need to distinguish between R1 and the response here. R1 is
        ; guaranteed to have a 0 in the top bit, so we put a 1 here. The data
        ; response token has bit 7 unspecified.
        tba
        ora a,#$80
        psh a
        jsr wait_busy
        pul a
        bra .end

.no_error:
        jsr wait_busy
        clr a
.end:
        jsr spi_end_command
        jsr spi_send_byte_ff
        rts

; - Internal implementation ----------------------------------------------------

; Wait for an R1 response from the card, abandon after 10 tries. R1 is returned
; in A, clobbers B, X.
get_r1:
        ldx #10
.loop:
        jsr spi_receive_byte
        tst a
        bpl .end                ; top bit is clear: R1
        dex
        bne .loop
.end:
        rts

; Wait for the start block marker or a data error token and return it in A. We
; repeat 500 times as it can take a while for a card to be ready to
; read. Clobbers B, X.
get_start_block:
        ldx #500
.loop:
        jsr spi_receive_byte
        cmp a,#$ff
        bne .end
        dex
        bne .loop
.end:
        rts

; Wait for a data response token from the card. Return it in A. Clobbers B, X.
get_data_response:
        ldx #500
.loop:
        jsr spi_receive_byte
        cmp a,#$ff
        bne .end
        dex
        bne .loop
.end
        rts

; Loop for as long as we get all 0 responses from the card. Clobbers A, B, X.
wait_busy:
        jsr spi_receive_byte
        tst a
        beq wait_busy
        rts

; Send GO_IDLE_STATE, response R1 is returned in A. Clobbers B, X.
send_cmd0:
        jsr spi_start_command
        lda #$40 + 0
        jsr spi_send_byte

        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00

        ; CRC, needs to be correct
        lda #$95
        jsr spi_send_byte

        jsr get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff
        rts

; Send SEND_IF_COND, response R1 is returned in A. Clobbers B, X.
send_cmd8:
        jsr spi_start_command
        lda #$40 + 8
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

        ; 5 bytes of response, R1 + 4 bytes we ignore
        jsr get_r1

        ; Note: the calls from here to rts all leave A intact
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte

        jsr spi_end_command

        jsr spi_send_byte_ff
        rts

; Send CMD55 (APP_CMD). Clobbers B, X, returns R1 in A.
send_cmd55:
        jsr spi_start_command
        lda #$40 + 55
        jsr spi_send_byte
        ; 4 random bytes (command doesn't care)
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
       ; CRC (not checked)
        jsr spi_send_byte_00

        jsr get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff
        rts

; Send CMD58 (READ_OCR). Returns either R1 (on failure) or the top OCR byte in
; A. The top two bits of the OCR are 1 in an SDHC card after initialization. R1
; would have the top bit 0. Clobbers B, X.
send_cmd58:
        ; Send CMD58 (read OCR): $7a $xx $xx $xx $xx $xx
        ;   - Response R3: R1 + 32bit OCR
        ;   - CCS bit is OCR[30] (1 == HC card, 512 bytes sector size fixed)
        jsr spi_start_command
        lda #$40 + 58
        jsr spi_send_byte
        ; 4x don't care
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        ; CRC (not checked)
        jsr spi_send_byte_00

        jsr get_r1
        bne .end

        ; Top byte is sent first
        jsr spi_receive_byte
        ; Discard the next 3 bytes. None of the commands below touch A.
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_discard_byte
        jsr spi_end_command
        jsr spi_send_byte_ff
.end:
        rts

; SD_SEND_OP_COND. Returns R1 in A. Clobbers B, X.
send_acmd41:
        jsr send_cmd55

        jsr spi_start_command
        lda #$40 + 41
        jsr spi_send_byte

        lda #$40
        jsr spi_send_byte
        ; 3 more random bytes
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        jsr spi_send_byte_00
        ; CRC (not checked)
        jsr spi_send_byte_00

        jsr get_r1

        jsr spi_end_command

        jsr spi_send_byte_ff
        rts


        ENDSECTION sd_card

        endif
