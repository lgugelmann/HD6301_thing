        cpu 6301

        include ../stdlib.inc

        SECTION io_test
        PUBLIC io_test_start

io_test_start:
        jsr sd_init
        jsr file_init
        ldx #0
        jsr file_cd
        ;jsr file_ls

.loop:
        jsr getchar
        cmp a,#'l'
        bne +
        jsr file_ls
        bra .loop
+
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
        rts

.failed_initialization:
        ldx #.failed_string
        jsr putstring
        rts

.failed_string:
        byt "Failed to initialize SD card\n\0"

.success_string:
        byt "SD card initialized successfully\n\0"

FILE_ATTR_SUBDIR = $10

        ; Indexed read of 2 little endian bytes from index 'offset' and stored
        ; in 'destination'
read16_le macro offset, destination
        lda offset,x
        sta destination+1
        lda offset+1,x
        sta destination
        endm

read8 macro offset, destination
        lda offset,x
        sta destination
        endm

        reserve_memory fat_sd_buffer, 512
        reserve_memory fat_current_dir_sector, 2
        reserve_memory fat_current_dir_buffer, 512
        reserve_memory fat_scratch, 2

        ; We make use of the specific layout above to place guard bytes into
        ; fat_scratch.
        if fat_scratch <> fat_current_dir_buffer + 512
          error "fat_scratch needs to be right after fat_current_dir_buffer"
        endif

        reserve_memory fat_sectors_per_cluster, 1
        reserve_memory fat_reserved_sectors, 2
        reserve_memory fat_sectors_per_fat, 2
        reserve_memory fat_root_dir_sector, 2



; Prepare to read a FAT16 partition on an initialized SD card
file_init:
        ldx #0000
        stx sd_card_block_address
        stx sd_card_block_address + 2

        ldx #fat_sd_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne read_error

        ldx #fat_sd_buffer
        read8 $0d, fat_sectors_per_cluster
        read16_le $0e, fat_reserved_sectors
        read16_le $16, fat_sectors_per_fat

        ; Number of FAT copies. We assume it's always 2. Bail out if it isn't.
        lda $10,x
        cmp a,#2
        bne .fat_copies_error

        ; Compute the start of the root directory:
	; fat_reserved_sectors + FAT region size
        ldd fat_sectors_per_fat
        asld                    ; multiply by 2
        addd fat_reserved_sectors
        std fat_root_dir_sector

        rts

.fat_copies_error_string:
        byt "Wrong number of FAT copies. Expected 2.\n\0"
.fat_copies_error:
        ldx #.fat_copies_error_string
        jsr putstring
        rts

read_error:
        ldx #.read_error_string
        jsr putstring
        rts
.read_error_string:
        byt "Failed to read from SD card\n\0"

; Change working directory to the directory entry at the cluster number in X. If
; the number is 0, change to the root directory instead.
file_cd:
        cpx #0
        bne .end

        ; Load the first directory entry
        stx sd_card_block_address ; X is 0 here
        ldx fat_root_dir_sector
        stx sd_card_block_address + 2
        stx fat_current_dir_sector

        ldx #fat_current_dir_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne read_error

.end:
        rts

; List the contents of the current directory. Assumes that file_cd has been
; called at least once.
file_ls:
        ; Place a 0 guard byte into fat_scratch to ensure the loop below
        ; terminates even if the directory table is full.
        clr fat_scratch
        ldx #fat_current_dir_buffer
.loop:
        ldb 0,x
        beq .end                ; 0 indicates that no more entries follow

        cmp b,#$2e              ; dot entry, either . or ..
        bne +
        jsr print_dot_entry
        bra .next
+
        cmp b,#$e5              ; deleted entry
        bne +
        bra .next
+
        jsr print_normal_entry  ; normal file or subdirectory
.next:
        ldb #32
        abx
        bra .loop

.end:
        rts

print_dot_entry:
        lda 1,x
        jsr putchar
        lda 2,x
        jsr putchar
        rts

print_normal_entry:
        pshx

        ; Print the 8 characters of the file / directory name
        ldb #8
.loop:
        lda 0,x
        jsr putchar
        inx
        dec b
        bne .loop

        ; Space
        lda #' '
        jsr putchar

        ; The 3 characters of the extension
        lda 0,x
        jsr putchar
        lda 1,x
        jsr putchar
        lda 2,x
        jsr putchar

        ; File attributes
        ldb 3,x
        and b,#FILE_ATTR_SUBDIR
        beq .end

        ldx #.dirstring
        jsr putstring

.end:
        jsr put_newline
        pulx
        rts

.dirstring:
        byt " <DIR>\0"


        ENDSECTION
