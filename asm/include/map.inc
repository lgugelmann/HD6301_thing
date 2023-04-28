        ifndef __map_inc
__map_inc = 1

        include macros

        SECTION map
        PUBLIC map_find

; A utility to find entries in a map stored as a linked list with the following
; format:
; - Every entry has
;   - 2 bytes address of the next map entry
;   - a 0-terminated string as key
;   - a number of payload bytes after the key
; - The last entry only has the 'next' bytes, and those are $0000.
; - An empty map only contains those 2 bytes set to $0000.
;
; This function uses local storage during operation. It should not be used
; during interrupts or care needs to be taken that the user program is not
; executing it.
;
; Function parameters:
; - X: a pointer to an entry of the map (typically the first)
; - On the stack two bytes (high, low) pointing to a 0-terminated or
;   space-terminated string of characters to search in the map.
;
; Function return:
; - On return C is set if there was a match, and cleared otherwise.
; - If there was a match:
;   - X points to the 1st byte past the 0-terminated key in the map
;   - The top 2 bytes on the stack point to the first byte after the input
;     string. (This is be either a 0 byte or a space.)
; - If there was no match:
;   - X is indetermined
;   - No values are put on the stack
;
        reserve_system_memory input_string_start,2
        reserve_system_memory input_string_ptr,2
        reserve_system_memory map_ptr,2
        reserve_system_memory next_entry_ptr,2
        reserve_system_memory return_ptr,2
map_find:
        stx map_ptr

        ldx 0,x                 ; Load the pointer to the next entry
        stx next_entry_ptr

        ; The pointer to the input string to match against keys is on the stack,
        ; but so is the return address. Put that one away for later.
        pulx
        stx return_ptr
        ; Get the actual input string
        pulx
        stx input_string_start
        stx input_string_ptr
        psh a
        psh b

        ldx map_ptr
        inx                     ; Skip the two 'next entry' bytes
        inx
        stx map_ptr

.entry_match_loop:
        ldx input_string_ptr
        lda 0,x
        ldx map_ptr
        ldb 0,x

        ; If B is 0 we found a key boundary.
        beq .key_boundary

        cba                     ; Compare input and key characters
        bne .next_entry         ; Mismatch, try the next map entry

        ; Character match, move on to the next character.
        inx
        stx map_ptr
        ldx input_string_ptr
        inx
        stx input_string_ptr
        bra .entry_match_loop

.key_boundary:
        ; The input string is either 0 or space-terminated. Both are valid
        ; matches for a 0 character in the key.
        ora #" "
        cmp a,#" "              ; This is true for A=0 and A=' '
        bne .next_entry         ; No match, try the next entry

.match:
        pul b
        pul a

        inx
        stx map_ptr             ; This is now one byte past the key end

        ; Push our return value on the stack
        ldx input_string_ptr
        pshx

        ; Put the return address back
        ldx return_ptr
        pshx

        ; And finally restore X to one past the key
        ldx map_ptr
        sec                     ; Indicate a match
        rts

.next_entry:
        ; Reset the input string pointer to the start and try matching the next
        ; entry.
        ldx input_string_start
        stx input_string_ptr

        ldx next_entry_ptr
        ldd 0,x                 ; Load the next item pointer
        beq .no_match           ; If it's $0000 we reached the end
        std next_entry_ptr

        inx
        inx
        stx map_ptr
        bne .entry_match_loop

.no_match:
        pul b
        pul a
        ; restore the return pointer we pulled off the stack earlier
        ldx return_ptr
        pshx
        clc                     ; Clear carry to indicate no match
        rts

        ENDSECTION

        endif
