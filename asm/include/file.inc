        ifndef __file_inc
__file_inc = 1

        include macros

        SECTION file
        PUBLIC file_init
        PUBLIC file_ls
        PUBLIC file_cd

FILE_ATTR_SUBDIR = $10

        reserve_memory fat_sd_buffer, 512
        reserve_memory fat_current_dir_buffer, 512
        ; Some extra scratch space for various operations
        reserve_memory fat_scratch, 6

        ; We make use of the specific layout above to place guard bytes into
        ; fat_scratch.
        if fat_scratch <> fat_current_dir_buffer + 512
          error "fat_scratch needs to be right after fat_current_dir_buffer"
        endif

        ; sectors per cluster is actually 1 byte in FAT, but we use 2 so we can
        ; load this in X easily.
        reserve_memory fat_sectors_per_cluster, 2
        reserve_memory fat_reserved_sectors, 2
        reserve_memory fat_sectors_per_fat, 2
        reserve_memory fat_root_dir_start_sector, 2
        reserve_memory fat_data_start_sector, 2

        ; Indexed read of 2 little endian bytes from index 'offset' and stored
        ; in 'destination'
read16_le macro offset, destination
        lda offset,x
        sta destination+1
        lda offset+1,x
        sta destination
        endm

file_init:
        jsr sd_card_initialize
        ldx #0000
        stx sd_card_block_address
        stx sd_card_block_address + 2

        ldx #fat_sd_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne read_error

        ldx #fat_sd_buffer
        ; Bytes per sector. We only support 512, or $00 $02 little endian.
        lda $0b,x
        bne .fat_sector_size_error
        lda $0c,x
        cmp a,#$02
        bne .fat_sector_size_error

        ; Number of FAT copies. We assume it's always 2. Bail out if it isn't.
        lda $10,x
        cmp a,#2
        bne .fat_copies_error

        ; We use 2 bytes for this so we can put it into X easily. The value from
        ; the parameter block goes into the second byte.
        clr fat_sectors_per_cluster
        lda $0d,x
        sta fat_sectors_per_cluster+1
        read16_le $0e, fat_reserved_sectors
        read16_le $16, fat_sectors_per_fat

        ; Number of root directory entries. Don't support 2k or more.
        read16_le $11, fat_scratch
        xgdx
        cpx #2047
        bgt .fat_root_directory_entries_error

        ; Compute the start of the root directory:
        ; fat_reserved_sectors + FAT region size
        ldd fat_sectors_per_fat
        asld                    ; multiply by 2
        addd fat_reserved_sectors
        std fat_root_dir_start_sector
        ; Compute the start of the data region:
        ;  fat_data_start_sector = fat_root_dir_start_sector + root sector size
        ; With:
        ;   root_sector_size = (32 * number_of_root_entries) / sector size
        ; As sector size is 512, this is equivalent to
        ;   root_sector_size = number_of_root_entries >> 4
        ; We have less than 2048 entries, so the sector size always fits into
        ; one byte.
        xgdx                    ; Move fat_root_dir_start_sector to X
        ldd fat_scratch         ; Holds the number of root dir entries
        lsrd
        lsrd
        lsrd
        lsrd                    ; >> 4 done, now every nonzero bit is in B only
        abx                     ; And the addition is done here
        stx fat_data_start_sector

        ldx #0
        jmp file_cd_cluster
        ; rts

.fat_copies_error:
        ldx #fat_copies_error_string
        jmp putstring

.fat_sector_size_error:
        ldx #fat_sector_size_error_string
        jmp putstring

.fat_root_directory_entries_error:
        ldx #fat_root_directory_entries_error_string
        jmp putstring

read_error:
        ldx #read_error_string
        jmp putstring

; Change working directory to the subdirectory of the current working directory
; that matches the name passed in as a 0-terminated string whose start address
; is in X. Clobbers A, B, X.
file_cd:
        jsr validate_name
        bne .error
        ; We're going to use the 6 bytes of scratch as:
	; 0,1: $0000 as guard bytes
        ; 2,3: address of the searched-for string
        ; 4,5: scratch space
        stx fat_scratch + 2
        stx fat_scratch + 4
        ; Place a 0 guard byte into fat_scratch to ensure the loop below
        ; terminates even if the directory table is full.
        clr fat_scratch
        ldx #fat_current_dir_buffer
.loop:
        ldb 0,x
        beq .error              ; 0 indicates that no more entries follow

        cmp b,#$e5              ; deleted entry
        beq .next

        ; Look into the attributes byte to see if it's a subdirectory. These are
        ; after the 8+3 name bytes.
        ldb 11,x
        and b,#FILE_ATTR_SUBDIR
        beq .next

        pshx                    ; Store the start of the directory entry
        ; Check if the name matches. X is at the start of the directory entry.
.name_loop:
        lda 0,x
        inx
        pshx

        ldx fat_scratch + 4
        ldb 0,x
        ; We found the end of the searched-for string after we matched all
        ; previous characters.
        beq .found_match
        inx
        stx fat_scratch + 4
        cba
        bne .no_match

        pulx
        bra .name_loop
.no_match:
        ; Restore the scratch space to start of the searched-for string.
        ldx fat_scratch + 2
        stx fat_scratch + 4
        pulx                    ; Pull the (dirtied) directory entry
        pulx                    ; Get the clean one
.next:
        ldb #32
        abx
        bra .loop

.error:
        ldx #.error_string
        jmp putstring
.error_string:
        byt "Could not find directory\0"

.found_match:
        pulx                    ; Get the dirtied directory entry
        pulx                    ; Get the start of the dir entry
        ldb $1a,x               ; Get the cluster number, little endian
        lda $1b,x
        xgdx
        jmp file_cd_cluster
        ; rts

; Change working directory to the directory entry at the cluster number in X. If
; the number is 0, change to the root directory instead.
file_cd_cluster:
        cpx #0
        bne .nonzero_cluster

        ; Load the root directory entry
        stx sd_card_block_address ; X is 0 here
        ldx fat_root_dir_start_sector
        stx sd_card_block_address + 2
        bra .load_dir_data

.nonzero_cluster:
        jsr fat_cluster_to_sector

.load_dir_data:
        ldx #fat_current_dir_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        beq .end
        jmp read_error
.end:
        rts

; Takes a cluster number in X and turns it into a sector number stored in
; sd_card_block_address. Clobbers A, B, X.
fat_cluster_to_sector:
        ; sector = fat_data_start_sector + (N - 2) * fat_sectors_per_cluster
        clr sd_card_block_address
        clr sd_card_block_address + 1
        clr sd_card_block_address + 2
        clr sd_card_block_address + 3
        dex
        dex                     ; N-2
        stx sd_card_block_address + 2
        xgdx
        ldx fat_sectors_per_cluster
        dex
.loop:
        addd sd_card_block_address + 2
        dex
        bne .loop

        addd fat_data_start_sector
        std sd_card_block_address + 2
        rts

; List the contents of the current directory. Assumes that file_cd* has been
; called at least once. Clobbers A, B, X.
file_ls:
        ; Place a 0 guard byte into fat_scratch to ensure the loop below
        ; terminates even if the directory table is full.
        clr fat_scratch
        ldx #fat_current_dir_buffer
.loop:
        ldb 0,x
        beq .end                ; 0 indicates that no more entries follow

        cmp b,#$e5              ; deleted entry
        beq .next
        jsr print_entry
.next:
        ldb #32
        abx
        bra .loop

.end:
        rts

print_entry:
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

; Checks whether the string pointed to by X is 0 terminated at or before byte
; 12. If it isn't, it can't possibly be a valid 8+3 file or directory name. Z is
; set if the name is valid, cleared if it isn't.
validate_name:
        pshx
        ldb #12
.loop:
        lda 0,x
        beq .found_zero
        inx
        dec b
        bne .loop

        tst a                   ; if we got here, this clears Z
.found_zero:
        pulx                    ; pulx does not affect Z
        rts

fat_copies_error_string:
        byt "Wrong number of FAT copies. Expected 2.\n\0"
fat_sector_size_error_string:
        byt "Wrong bytes per sector count. Only 512 is supported.\n\0"
fat_root_directory_entries_error_string:
        byt "Too many root directory entries. Max is 2048.\n\0"
read_error_string:
        byt "Failed to read from SD card\n\0"

        ENDSECTION

        endif
