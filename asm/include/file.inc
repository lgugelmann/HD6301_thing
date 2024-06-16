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
        PUBLIC file_at_eof

;
; Module to handle FAT16 file I/O
;
; Limitations:
; - The sector size has to be 512 (almost universally true).
; - The number of FAT copies has to be exactly 2 (almost always true).
; - At most 255 sectors per FAT
; - At most 2047 root directory entries.
; - File size at most 0xffff bytes (64k), this also implies at most 128 clusters
;   per file.
; - Only clusters 0..254 can be used in the FAT

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
        ; Current directory sector buffer
        ;
        reserve_system_memory fat_current_dir_buffer, FAT_SECTOR_SIZE
        ; A guard byte after the current dir buffer that we can use as a trap to
        ; detect when we've gone over all entries. Is 0 for now to indicate 'no
        ; more entries'.
        reserve_system_memory fat_dir_guard_byte, 1

        ; Check that we really did get the layout we want
        if fat_dir_guard_byte <> fat_current_dir_buffer + FAT_SECTOR_SIZE
          error "fat_dir_guard_byte not right after fat_current_dir_buffer"
        endif

        ;
        ; Parameters of the current partition
        ;
        reserve_system_memory fat_sectors_per_cluster, 1
        ; Sectors per cluster is a power of 2, the mask below is used to easily
        ; do a modulo of sectors per cluster.
        reserve_system_memory fat_sectors_per_cluster_mask, 1
        reserve_system_memory fat_reserved_sectors, 2
        reserve_system_memory fat_sectors_per_fat, 2
        reserve_system_memory fat_root_dir_start_sector, 2
        reserve_system_memory fat_data_start_sector, 2

        ;
        ; Buffer to hold the current FAT sector
        ;
        reserve_system_memory fat_buffer, FAT_SECTOR_SIZE
        ; The FAT sector loaded in fat_buffer as an offset from the FAT
        ; start. We only support up to 255 sectors per FAT so this is just one
        ; byte. $ff is explicitly not a valid sector number.
        reserve_system_memory fat_buffer_sector, 1

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

        clr fat_dir_guard_byte

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
        beq .no_sd_error
        jmp read_error

.no_sd_error:
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

        ; Sectors per cluster + a mask to perform modulo operations on sector
        ; per cluster.
        ldb $0d,x
        stb fat_sectors_per_cluster
        lsr b
        beq .mask_loop_end      ; in case fat_sectors_per_cluster is 1
        clr a
.mask_loop:
        aba                     ; B+A -> A
        lsr b
        bne .mask_loop
.mask_loop_end:
        sta fat_sectors_per_cluster_mask

        read16_le $0e, fat_reserved_sectors
        read16_le $11, fat_data_start_sector
        read16_le $16, fat_sectors_per_fat

        ; Number of root directory entries. Don't support 2k or more. It's
        ; almost always 512, so this isn't a limitation.
        ; We only need this temporarily - so we're using the start sector as
        ; scratch space.
        ldx fat_data_start_sector
        cpx #2047
        bgt .fat_root_directory_entries_error

        ; For now we only support at most 255 sectors per FAT so the sector
        ; number fits into one byte plus $ff as a guard value.
        ldx fat_sectors_per_fat
        cpx #254
        bgt .fat_sectors_per_fat_error

        ; Compute the start of the root directory:
        ; fat_reserved_sectors + FAT region size
        xgdx                    ; Optimized: ldd fat_sectors_per_fat
        asld                    ; multiply fat_sectors_per_fat by 2 for 2 FATs
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
        ; Holds the number of root dir entries, see above
        ldd fat_data_start_sector
        lsrd
        lsrd
        lsrd
        lsrd                    ; >> 4 done, now every nonzero bit is in B only
        abx                     ; And the addition is done here
        stx fat_data_start_sector

        ; Put an invalid sector number fat_buffer_sector to make sure we load
        ; a valid FAT sector the first time we need it.
        lda #$ff
        sta fat_buffer_sector

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

.fat_sectors_per_fat_error:
        ldx #fat_sectors_per_fat_error_string
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
        pshx                    ; Name pointer
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

        pshx                    ; File descriptor pointer

        ; Stack is now (top-of-stack at the bottom):
        ; - Name pointer
        ; - File descriptor pointer

        ; Get the directory entry for the file to open into X
        tsx
        ldx 2,x
        lda #FAT_DIR_ATTR_ARCHIVE ; See comments on .._ARCHIVE definition
        jsr find_dir_entry

        cpx #0
        beq .file_not_found_error

        pshx                    ; Put directory entry pointer on stack

        ; Put start cluster and sector numbers into FD struct
        lda FAT_DIR_CLUSTER_OFFSET+1,x
        ldb FAT_DIR_CLUSTER_OFFSET,x

        tsx
        ldx 2,x                 ; FD pointer
        std FD_START_CLUSTER_OFFSET,x

        xgdx                    ; Cluster number -> X
        jsr fat_cluster_to_sector
        ; Store the start sector, it's put into D by the above.
        tsx
        ldx 2,x                 ; FD pointer
        std FD_START_SECTOR_OFFSET,x

        ; Put the file size in FD struct
        tsx
        ldx 0,x                 ; Dir entry pointer
        lda FAT_DIR_SIZE_OFFSET+1,x
        ldb FAT_DIR_SIZE_OFFSET,x

        tsx
        ldx 2,x                 ; FD pointer
        std FD_FILE_SIZE_OFFSET,x

        lda #FD_STATE_OPEN
        sta FD_STATE_OFFSET,x
        clr a
        sta FD_READ_PTR_OFFSET,x
        sta FD_READ_PTR_OFFSET+1,x

        drop2                   ; Dir entry pointer
        drop2                   ; FD pointer
        drop2                   ; name pointer
        rts

.file_not_found_error:
        lda #128
        drop2                   ; Drop dir entry pointer
        drop2                   ; Drop FD pointer
        rts

.no_free_fd_error:
        lda #129
        ; Undo psh A, B, X
        drop2
        drop2
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
        abx                     ; FD entry -> X

        ; Get the status byte
        lda 0,x
        and a,#FD_STATE_OPEN
        beq .error_fd_not_open

        pshx                    ; FD struct address

        ldd FD_READ_PTR_OFFSET,x
        ; read_ptr is either at 512 bytes offset, or at the end of the
        ; file. Check if we're at the end-of-file case.
        subd FD_FILE_SIZE_OFFSET,x
        beq .no_bytes_to_read

        ; read_ptr is a multiple of 512 here. We get the logical sector number
        ; by dividing by 512.
        lda FD_READ_PTR_OFFSET,x ; Get the high byte of read_ptr
        lsr a                    ; A = read ptr / 512

        psh a                   ; Store the sector offset

        ; Divide by fat_sectors_per_cluster to get the cluster number. Sectors
        ; per cluster is guaranteed to be a power of 2, at least 1, at most 128.
        ldb fat_sectors_per_cluster
        lsr b
        beq +
.sector_div_loop:
        lsr a
        lsr b
        bne .sector_div_loop
+
        tst a
        ; If we're in the first cluster of the file we just need to add the
        ; offset and don't need to go through the FAT.
        bne .nonzero_cluster_offset
        pul b                   ; Get the sector offset into B
        ldx FD_START_SECTOR_OFFSET,x
        abx
        xgdx
        bra .read

.nonzero_cluster_offset:
        ; X contains FD pointer
        ; A contains the cluster offset from the start
        ; We need to do A many jumps in the FAT
        ldx FD_START_CLUSTER_OFFSET,x
        jsr fat_cluster_offset
        jsr fat_cluster_to_sector

        ; We computed the start sector for the given cluster number, but we may
        ; be a few sectors into that cluster already. Fix that here.
        xgdx                    ; Sector number in D -> X
        pul b                   ; Get the sector offset
        and b,fat_sectors_per_cluster_mask
        abx
        xgdx                    ; Sector number -> D
.read:
        ; Assumes the block address is in D
        clr sd_card_block_address
        clr sd_card_block_address + 1
        std sd_card_block_address + 2

        tsx
        ldx 0,x
        ldd FD_IO_ADDRESS_OFFSET,x
        std sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne .error_sd_issue

        ; Move the read pointer. This is either a whole sector size, or - if we
        ; reached the end of the file - the file size.
        tsx
        ldx 0,x                 ; FD struct address
        ldd FD_READ_PTR_OFFSET,x
        add a,#2                ; Optimized addd #512
        std FD_READ_PTR_OFFSET,x

        subd FD_FILE_SIZE_OFFSET,x
        bgt .at_file_end

        ldx #FAT_SECTOR_SIZE
        bra .end

.at_file_end
        ; We're at the file end - the add 512 moved the read_ptr past the file
        ; size. Fix that.
        ldd FD_FILE_SIZE_OFFSET,x
        std FD_READ_PTR_OFFSET,x
        and a,#$01              ; read bytes count is D mod 512
        xgdx                    ; Put the read bytes in X
.end:
        drop2                   ; Drop the FD struct address
        pul a                   ; FD number
        rts

.no_bytes_to_read:
        ldx #0                  ; No bytes read
        bra .end

.error_invalid_fd:
        ldx #0
        lda #128
        rts

.error_fd_not_open:
        ins                     ; drop A
        ldx #0
        lda #129
        rts

.error_sd_issue:
        drop2                   ; drop FD struct address
        ins                     ; drop A
        ldx #0
        lda #130
        rts

.error_file_too_long:
        ins                     ; drop A
        drop2                   ; drop FD struct address
        ins                     ; drop A
        ldx #0
        lda #131
        rts

; Looks at the file descriptor in A and sets Z if the read pointer is at the end
; of the file, and clears it if not. A has to be valid. Keeps all registers
; intact.
file_at_eof:
        pshx
        psh b
        psh a

        ldb #FD_STRUCT_SIZE
        mul                     ; Fits into B, max table size is 255

        ; Point X to the start of the file descriptor table entry
        ldx #fd_table
        abx

        ; Compare high bytes
        ldd FD_FILE_SIZE_OFFSET,x
        subd FD_READ_PTR_OFFSET,x ; Sets Z as per spec

        pul a                   ; None of these touch Z
        pul b
        pulx
        rts


; --- Internal helpers ---------------------------------------------------------

; Computes the cluster number corresponding to the A-th cluster in the FAT chain
; starting at cluster X. Returns the cluster number in X. A is 0 on success, and
; contains an error number >127 otherwise. Z is also guaranteed to be set on
; success. Clobbers A, B.
fat_cluster_offset:
        ; Figure out if we have the right FAT sector loaded. Each sector holds
        ; 256 clusters. For now we only support cluster numbers 2..254.
        cpx #254
        bgt .cluster_too_high_error

        pshx                    ; Start cluster number
        psh a                   ; Number of hops in the chain

        cpx #0
        beq .cluster_found

        ; Make sure we have the right FAT sector loaded. For now that's always
        ; sector 0 - we don't yet support more than that.
        ldb fat_buffer_sector
        cmp b,#0
        beq .fat_buffer_ready

        ; The FAT is stored after fat_reseved_sectors many sectors from the
        ; start.
        clr sd_card_block_address
        clr sd_card_block_address + 1
        ldx fat_reserved_sectors
        stx sd_card_block_address + 2

        ldx #fat_buffer
        stx sd_card_io_buffer_address

        jsr sd_card_read_block
        tst a
        bne .fat_buffer_read_error
        clr fat_buffer_sector
        bra .fat_buffer_ready

.fat_buffer_read_error:
        ; Error out in case of an SD card read error.
        ; TODO: we really need better error handling / reporting.
        ins                     ; Drop A
        drop2                   ; Drop cluster number
        lda #133
        rts

.fat_buffer_ready:
        ; Hop around in the FAT until we reach the desired cluster. After this
        ; code 1,x from top-of-stack will contain the correct cluster number.
        tsx
        ldd 1,x                 ; Current cluster number
.loop:
        ; Cluster N is stored at 2*N bytes from the start
        asld
        addd #fat_buffer
        xgdx
        ; Little-endian read of the next cluster number
        lda 1,x
        ldb 0,x
        cmp a,#$ff              ; End-of-chain, kind of unexpected?
        beq .cluster_found
        tsx
        std 1,x
        dec 0,x                 ; Number of hops
        bne .loop

.cluster_found:
        tsx
        ldx 1,x

        ins                     ; Drop hop count
        drop2                   ; Drop cluster number
        clr a                   ; Sets Z too
        rts

.cluster_too_high_error:
        lda #132
        rts


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

        clr a
        ldb fat_sectors_per_cluster ; Guaranteed to be a power of 2
        lsr b
        beq +
.loop:
        xgdx
        asld
        xgdx
        lsr b
        bne .loop
+
        xgdx
        addd fat_data_start_sector
        std sd_card_block_address + 2
        rts

; Find the entry in the current directory that matches the name passed in as a
; 0-terminated string whose start address is in X, and the file attributes mask
; passed into A. If a match is found, X contains the address of the start of
; that entry, if not, X is 0. Clobbers A, B, X.
find_dir_entry:
        psh a                   ; Attributes
        pshx                    ; Copy 1 of the search string pointer
        pshx                    ; Copy 2: this one gets incremented
        ldx #fat_current_dir_buffer
        pshx

.dir_entry_loop:
        ; Precondition: X points to the start of a directory entry
        ldb 0,x                 ; First directory entry byte
        beq .error              ; 0 indicates that no more entries follow

        cmp b,#$e5              ; deleted entry
        beq .next_dir_entry

        ldb FAT_DIR_ATTR_OFFSET,x
        tsx
        and b,6,x
        beq .next_dir_entry     ; Attributes don't match

        ldb #8+3
.name_loop:
        tsx
        ldx 2,x                 ; Load search string pointer
        lda 0,x                 ; A is current search string char
        beq .match_directory_space ; End of search string, look for spaces

        tsx
        ldx 0,x                 ; Load dir entry pointer
        cmp a,0,x               ; Compare A to character at dir entry pointer
        beq .name_loop_tail     ; Match: increment pointers, continue the name
                                ; comparison loop.

        ; Not equal, we could be dealing with an extension-separator dot. Note:
        ; We don't do special extension dot handling for directories. Only exact
        ; textual matches. That makes it easier to deal with '.' and '..' dirs.
        cmp a,#'.'
        bne .next_dir_entry     ; Search string has no dot, mismatch.

        ; If we're looking for directories, don't do extension dot handling.
        lda #FAT_DIR_ATTR_SUBDIR
        tsx
        and a,6,x
        bne .next_dir_entry

        bra .extension_dot_found

.name_loop_tail:
        tsx
        ; Increment search string pointer. This needs 16 bit math.
        inc16 2,x
.name_loop_tail_no_str_increment:
        ; Increment dir entry pointer - it's safe to only do the low byte as
        ; they are 32-byte aligned and we only do this at most 11 times.
        tsx
        inc 1,x
        dec b
        bne .name_loop
        ; We got past all 11 characters without bailing out. We found our entry!
        ; Dir entry pointers are 32-byte aligned. We can get back to the start
        ; of the entry by just clearing out the low 5 bits.
        ldd 0,x                 ; Load dir entry pointer
        and b,#$e0              ; Clear out low 32 bits
        xgdx                    ; move to X
        bra .end

.extension_dot_found:
        cmp b,#4                ; Still not in the extension part
        bgt .match_directory_space
        ; We now have two cases:
        ; - The file name is <8 chars: 'TEST.TXT' / 'TEST    TXT'
        ; - The file name is 8 chars:  'LONGTEST.TXT' / 'LONGTESTTXT'
        ; We need to increment the dir entry pointer past a ' ' only in the
        ; first case.
        tsx
        ldx 0,x                 ; Load the dir entry pointer
        lda 0,x                 ; The current dir entry char
        cmp a,#' '
        beq .name_loop_tail     ; a space, skip it
        ; A bit hacky...
        tsx
        dec 1,x                 ; Will be undone by .name_loop_tail.
        bra .name_loop_tail

; The search string has a 0 or '.', so the directory entry has to be ' ' or we
; can't match.
.match_directory_space:
        tsx
        ldx 0,x                 ; Dir entry pointer
        lda 0,x
        cmp a,#' '
        beq .name_loop_tail_no_str_increment
        ; Mismatch: move to the next directory entry
.next_dir_entry:
        tsx
        ldd 4,x                 ; Clean search string pointer
        std 2,x
        ldd 0,x                 ; Dir entry pointer -> D
        and b,#$e0              ; Clear out the low 32 bits to get to the start
                                ; of the entry.
        addd #FAT_DIR_ENTRY_SIZE
        std 0,x
        xgdx                    ; .dir_entry_loop requires X to be a directory
                                ; entry pointer.
        bra .dir_entry_loop

.error:
        ldx #0
.end:
        drop2                   ; Dir pointer
        drop2                   ; String pointer, copy 2
        drop2                   ; String pointer, copy 1
        ins                     ; attributes
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
fat_sectors_per_fat_error_string:
        byt "Too many sectors per FAT. Max is 254.\n\0"
read_error_string:
        byt "Failed to read from SD card\n\0"


        ENDSECTION

        endif
