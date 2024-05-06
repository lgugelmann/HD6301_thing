        cpu 6301

        include ../stdlib.inc

        SECTION io_test
        PUBLIC io_test_start

        reserve_memory sd_buffer, 512

io_test_start:
.loop:
        jsr getchar
        beq .loop

        cmp a,#'r'
        bne .loop
        jsr sd_init
        bra .loop

        rts

sd_init:
        jsr sd_card_initialize
        tst a
        beq .init_success
        jmp .failed_initialization

.init_success:
        ldx #.success_string
        jsr putstring

        lda #'r'
        jsr putchar

        ldx #sd_buffer
        stx sd_card_io_buffer_address

        lda #0
        sta sd_card_block_address
        sta sd_card_block_address + 1
        sta sd_card_block_address + 2
        lda #$01
        sta sd_card_block_address + 3

        jsr sd_card_read_block
        tst a
        bne .error
        jsr test_print_sd_block

        lda #'e'
        jsr putchar
        lda #KEY_ENTER
        jsr putchar

        clr sd_card_block_address + 3
        jsr sd_card_read_block
        tst a
        bne .error
        jsr test_print_sd_block

        lda #$01
        sta sd_card_block_address + 3
        jsr sd_card_write_block
        tst a
        bne .error

        jsr sd_card_read_block
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
        ldx #sd_buffer
.loop:
        lda 0,x
        jsr putchar_hex
        inx
        cpx #sd_buffer+512
        bne .loop
        lda #KEY_ENTER
        jsr putchar
        rts



        ENDSECTION
