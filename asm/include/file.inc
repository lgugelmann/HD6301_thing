        ifndef __file_inc
__file_inc = 1

        include macros

        SECTION file
        PUBLIC file_init
        PUBLIC file_ls
        PUBLIC file_cd
        PUBLIC file_open
        PUBLIC file_close
        PUBLIC file_read

; Technically this can change, but it's almost always 512. The code assumes that
; the sector size is 512 and the init code bails out if the partition is set up
; differently.
FAT_SECTOR_SIZE = 512

; Size of a directory entry
FAT_DIR_ENTRY_SIZE = 32
; -- Offsets into that directory entry
FAT_DIR_CLUSTER_OFFSET = $1a    ; First cluster number
FAT_DIR_ATTR_OFFSET = $0b       ; Attributes
FAT_DIR_SIZE_OFFSET = $1c       ; File size

; -- Masks for directory entry attribute bits
FAT_DIR_ATTR_SUBDIR  = $10
; Technically speaking this indicates that a file has never been 'archived',
; i.e. backed up. The OS is supposed to set it on creation or file modification,
; and only backup software or a special command is ever supposed to clear
; it. For our implementation we use it to mean 'regular file', which is much
; easier than the other option which is to handle the 'no bits set' case for
; directory entry attributes.
FAT_DIR_ATTR_ARCHIVE = $20

        ;
        ; Various buffers & scratch space
        ;
        reserve_system_memory fat_current_dir_buffer, FAT_SECTOR_SIZE
        ; Some extra scratch space for various operations
        reserve_system_memory fat_scratch, 6

        ; We make use of the specific layout above to place guard bytes into
        ; fat_scratch.
        if fat_scratch <> fat_current_dir_buffer + FAT_SECTOR_SIZE
          error "fat_scratch needs to be right after fat_current_dir_buffer"
        endif

        ;
        ; Parameters of the current partition
        ;

        ; sectors per cluster is actually 1 byte in FAT, but we use 2 so we can
        ; load this in X easily.
        reserve_system_memory fat_sectors_per_cluster, 2
        reserve_system_memory fat_reserved_sectors, 2
        reserve_system_memory fat_sectors_per_fat, 2
        reserve_system_memory fat_root_dir_start_sector, 2
        reserve_system_memory fat_data_start_sector, 2

        ;
        ; Data structures for file descriptors
        ;

        ; A file descriptor number is an index into a table of file descriptors
        ; where each entry has this format:
        ; - state: 1 byte, 0 means unused, 1 means in-use (to be expanded)
        ; - io_address: 2 bytes, the address to the 512 bytes buffer for I/O
        ;               operations.
        ; - start_cluster: 2 bytes, the FAT cluster number of the first cluster
        ;                  in the file.
        ; - start_sector: 2 bytes, the sectro number of the first sector in the
        ;                  file.
        ; - file_size: 2 bytes, the size of the file in bytes. This is actually
        ;              4 bytes but we only support files up to 64k.
        ; - read_ptr: 2 bytes, the location of the read pointer in reference to
        ;             the (logical) start of the file. Always guaranteed to be
        ;             at 512 byte boundaries except for the last sector in the
        ;             file, where it may have falue file_size % 512.

FD_STATE_OFFSET = 0
FD_IO_ADDRESS_OFFSET = FD_STATE_OFFSET + 1
FD_START_CLUSTER_OFFSET = FD_IO_ADDRESS_OFFSET + 2
FD_START_SECTOR_OFFSET = FD_START_CLUSTER_OFFSET + 2
FD_FILE_SIZE_OFFSET = FD_START_SECTOR_OFFSET + 2
FD_READ_PTR_OFFSET = FD_FILE_SIZE_OFFSET + 2
FD_STRUCT_SIZE = FD_READ_PTR_OFFSET + 2

        ; The max number of open files at any one time
FD_TABLE_ENTRIES = 2
FD_TABLE_SIZE = FD_TABLE_ENTRIES * FD_STRUCT_SIZE
        if FD_TABLE_SIZE > 255
          error "File descriptor table too big"
        endif
        reserve_system_memory fd_table, FD_TABLE_SIZE
FD_TABLE_END = fd_table + FD_TABLE_SIZE - 1

        ; File descriptor state masks
FD_STATE_OPEN = $01

        ; Indexed read of 2 little endian bytes from index 'offset' and stored
        ; in 'destination'
read16_le macro offset, destination
        lda offset,x
        sta destination+1
        lda offset+1,x
        sta destination
        endm

file_init:
        ; Zero out the file descriptor table
        lda #FD_TABLE_SIZE
        ldx #fd_table
.loop:
        clr 0,x
        inx
        dec a
        bne .loop

        jsr sd_card_initialize

        ; We're assuming the partition starts at the first sector, i.e. that the
        ; card has no MBR. TODO: handle the MBR case.
        ldx #0000
        stx sd_card_block_address
        stx sd_card_block_address + 2

        ; We're reusing the directory buffer to read the FAT parameters - we
        ; only need to do it once at the start, and it would be wasteful
        ; otherwise.
        ldx #fat_current_dir_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne read_error

        ldx #fat_current_dir_buffer
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

        ; Sectors per cluster is just one byte, but we use 2 bytes here so we
        ; can load it into X without jumping through hoops. That makes cluster
        ; math easier later. The value from the parameter block goes into the
        ; second byte.
        clr fat_sectors_per_cluster
        lda $0d,x
        sta fat_sectors_per_cluster+1
        read16_le $0e, fat_reserved_sectors
        read16_le $16, fat_sectors_per_fat

        ; Number of root directory entries. Don't support 2k or more. It's
        ; almost always 512, so this isn't a limitation.
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
        lda #FAT_DIR_ATTR_SUBDIR
        jsr find_dir_entry
        cpx #0
        beq .cd_error

        ; Get the cluster number into X, little endian
        lda FAT_DIR_CLUSTER_OFFSET+1,x
        ldb FAT_DIR_CLUSTER_OFFSET,x
        xgdx
        jmp file_cd_cluster
        ; rts

.cd_error:
        ldx #.cd_error_string
        jmp putstring

.cd_error_string:
        byt "Could not find directory\0"

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

; List the contents of the current directory. Assumes that file_cd* has been
; called at least once. Clobbers A, B, X.
file_ls:
        ; Place a 0 guard byte into fat_scratch to ensure the loop below
        ; terminates even if the directory table is full.
	; TDOO: figure out a guard value for this and load the next sector if
	; it's hit.
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

; Opens the file with name pointed to by X for reading. D needs to point to a
; memory region of 512 bytes for file I/O. Returns a file descriptor number
; <=127 in A on success, or >127 if an error occurred.
file_open:
        ; Store the inputs for later while we find an available FD
        pshx
        psh a
        psh b

        ldx #fd_table
        ldb #FD_STRUCT_SIZE
.loop:
        lda 0,x
        beq .found
        abx
        cpx #FD_TABLE_END
        bgt .no_free_fd_error
        bra .loop

.found:
        pul b
        pul a
        ; Store the address for the I/O memory
        std FD_IO_ADDRESS_OFFSET,x

        ; Put the FD pointer away for later - we need to avoid fat_scratch
        ; because it's clobbered by find_dir_entry.
        xgdx                    ; FD -> D
        pulx                    ; Get the name pointer (NP)
        xgdx                    ; NP -> D, FD -> X
        pshx                    ; push FD
        xgdx                    ; NP in X

        ; Get the directory entry for the file to open into X
        lda #FAT_DIR_ATTR_ARCHIVE ; See comments on .._ARCHIVE definition
        jsr find_dir_entry

        cpx #0
        beq .file_not_found_error

        ; Get the cluster number, little endian
        read16_le FAT_DIR_CLUSTER_OFFSET, fat_scratch + 2

        ; Get the file size
        read16_le FAT_DIR_SIZE_OFFSET, fat_scratch + 4

        pulx                    ; Get the file descriptor address in X
        ldd fat_scratch + 4
        std FD_FILE_SIZE_OFFSET,x
        ldd fat_scratch + 2
        std FD_START_CLUSTER_OFFSET,x

        pshx                    ; Put away FD pointer
        xgdx                    ; Put cluster offset into X
        jsr fat_cluster_to_sector
        ; Store the start sector, it's put into D by the above.
        pulx
        std FD_START_SECTOR_OFFSET,x

        lda #FD_STATE_OPEN
        sta FD_STATE_OFFSET,x
        clr a
        sta FD_READ_PTR_OFFSET,x
        sta FD_READ_PTR_OFFSET+1,x
        rts

.file_not_found_error:
        ins
        ins                     ; Drop a pushx
        lda #128
        rts

.no_free_fd_error:
        ; Undo psh A, B, X
        ins
        ins
        ins
        ins
        lda #129
        rts

; Closes the file with file descriptor A. Clobbers A, B.
; TODO: once we have writes, make sure to flush the data on close.
file_close:
        cmp a,#FD_TABLE_ENTRIES
        bge .end                ; A isn't pointing to a valid FD, ignore.
        ldb #FD_TABLE_SIZE
        mul                     ; Result is entirely in B, max size is <255
        ldx #fd_table
        abx                     ; X now points to our FD entry
        clr 0,x                 ; Zero out the state bit.
.end:
        rts

; Reads at most 512 many bytes from the file descriptor in A into the I/O memory
; area passed in at file open. Returns the number of bytes that are actually
; read as part of the file in X. If successful, A is untouched. On error A has a
; value >127. Clobbers B.
file_read:
        cmp a, #FD_TABLE_ENTRIES
        bge .error_invalid_fd

        psh a                   ; Store the FD number

        ldb #FD_STRUCT_SIZE
        mul                     ; Fits into B, max table size is 255

        ; Point X to the start of the file descriptor table entry
        ldx #fd_table
        abx

        ; Get the status byte
        lda 0,x
        and a,#FD_STATE_OPEN
        beq .error_fd_not_open

        ; Figure out which sector to read next. Easy case is read_ptr = 0, in
        ; which case it's just start_sector.
        ldd FD_READ_PTR_OFFSET,x
        beq .read_at_start

        ; TODO: implement reading files longer than 512 bytes!
        bra .error_file_too_long

.read_at_start:
        ldd FD_IO_ADDRESS_OFFSET,x
        std sd_card_io_buffer_address

        ldd FD_START_SECTOR_OFFSET,x
        clr sd_card_block_address
        clr sd_card_block_address + 1
        std sd_card_block_address + 2

        pshx
        jsr sd_card_read_block
        pulx
        tst a
        bne .error_sd_issue

        ; Move the read pointer. This is either a whole sector size, or (if we
        ; reached the end of the file), the file size.
        ldd FD_READ_PTR_OFFSET,x
        addd #FAT_SECTOR_SIZE

        ; The read_ptr is guaranteed to be at 512 byte boundaries here, so we
        ; can ignore the low byte. Load the high byte of the file size and
        ; compare.
        ldb FD_FILE_SIZE_OFFSET,x
        cba                     ; A-B
        bgt .at_end             ; read_ptr high byte is above size high byte.

        ; read_ptr is still at or below file size, so it's still at a 512 byte
        ; boundary. We can just clear B to get back to the correct value
        ; computed above.
        clr b
        std FD_READ_PTR_OFFSET,x
        ldx #FAT_SECTOR_SIZE    ; We read 512 bytes
        bra .end

.at_end:
        ldd FD_FILE_SIZE_OFFSET,x
        std FD_READ_PTR_OFFSET,x
        xgdx                    ; Put the read bytes in X
        bra .end

.end:
        pul a
        rts

.error_invalid_fd:
        ldx #0
        lda #128
        rts

.error_fd_not_open:
        pul a
        ldx #0
        lda #129
        rts

.error_sd_issue:
        pul a
        ldx #0
        lda #130
        rts

.error_file_too_long:
        pul a
        ldx #0
        lda #131
        rts

; --- Internal helpers ---------------------------------------------------------

; Takes a cluster number in X and turns it into a sector number stored in
; sd_card_block_address. The sector number is also returned in D. Clobbers A, B,
; X.
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

; Find the entry in the current directory that matches the name passed in as a
; 0-terminated string whose start address is in X, and the file attributes mask
; passed into A. If a match is found, X contains the address of the start of
; that entry, if not, X is 0. Clobbers A, B, X.
find_dir_entry:
        ; We're going to use the 6 bytes of scratch as:
	; 0  : $00 as guard byte so the directory loop terminates
        ; 1  : file attributes mask
        ; 2,3: address of the searched-for string
        ; 4,5: scratch space
        clr fat_scratch
        sta fat_scratch + 1

        ; Ensure the name has a 0 within the first 12 bytes, we need this to be
        ; true for the name loop below to terminate. Strings that don't meet
        ; that also can't be valid file names.
        jsr validate_name
        bne .error

        stx fat_scratch + 2
        stx fat_scratch + 4
        ldx #fat_current_dir_buffer
.loop:
        ldb 0,x
        beq .error              ; 0 indicates that no more entries follow

        cmp b,#$e5              ; deleted entry
        beq .next

        ; Look into the attributes bytes to see if they match.
        ldb FAT_DIR_ATTR_OFFSET,x
        and b,fat_scratch + 1
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
        ldb #FAT_DIR_ENTRY_SIZE
        abx
        bra .loop

.found_match:
        pulx                    ; Get the dirtied directory entry
        pulx                    ; Get the start of the dir entry
        rts

.error:
        ldx #0
        rts

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

        ; Check file attributes for a directory entry
        ldb 3,x
        and b,#FAT_DIR_ATTR_SUBDIR
        beq .end

        ldx #.dirstring
        jsr putstring

.end:
        jsr put_newline
        pulx
        rts

.dirstring:
        byt " <DIR>\0"

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
