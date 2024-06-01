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
;   - X is undefined
;   - No values are put on the stack
; - A, B are preserved
map_find:
        ; Stack layout (in memory order, top of stack at the bottom):
        ; - Start of search string (from user call)
        ; - Return address         (from jsr)
        ; - A, B
        ; - Map entry pointer
        ; - Next entry pointer
        ; - Search string pointer
        psh a
        psh b

        pshx                    ; Map entry pointer
        ldx 0,x                 ; Load the pointer to the next entry
        pshx                    ; Next entry pointer

        tsx
        ldx 8,x                 ; Start of search string
        pshx                    ; A copy, used as a moving search string ptr

        tsx
        ldd 4,x                 ; Map entry pointer
        addd #2                 ; Skip the two 'next entry' bytes
        std 4,x                 ; Map entry pointer

.entry_match_loop:
        tsx
        ldx 0,x                 ; Search string pointer
        lda 0,x                 ; Next search string character

        tsx
        ldx 4,x                 ; Map entry pointer
        ldb 0,x                 ; Next map entry character

        ; If B is 0 we found a key boundary.
        beq .key_boundary

        cba                     ; Compare input and key characters
        bne .next_entry         ; Mismatch, try the next map entry

        ; Character match, move on to the next character.
        tsx
        inc16 4,x               ; Map entry pointer
        inc16 0,x               ; Search string pointer
        bra .entry_match_loop

.key_boundary:
        ; The input string is either 0 or space-terminated. Both are valid
        ; matches for a 0 character in the key.
        ora #" "
        cmp a,#" "              ; This is true for A=0 and A=' '
        bne .next_entry         ; No match, try the next entry

.match:
        ; We need to set things up such that:
        ; - Map entry pointer +1 is in X
        ; - Search string pointer is top of stack after return
        ; - Restore A,B
        inx                     ; X is now one byte past the key end.
        xgdx                    ; X -> D
        tsx
        std 4,x                 ; Put it back on the stack

        ldd 0,x                 ; Search string pointer
        std 10,x                ; Put it above the return value

        drop2                   ; Drop search string pointer
        drop2                   ; Drop next entry pointer
        pulx                    ; Map entry pointer + 1

        pul b
        pul a

        sec                     ; Indicate a match
        rts

.next_entry:
        ; Reset the input string pointer to the start and try matching the next
        ; entry.
        tsx
        ldd 10,x
        std 0,x

        ldd 2,x                 ; Next entry pointer
        std 4,x                 ; Store as new map entry pointer
        xgdx                    ; Map entry pointer -> X
        ldd 0,x                 ; Load the next entry pointer
        beq .no_match           ; If it's $0000 we reached the end

        tsx
        std 2,x                 ; Store the new next entry pointer

        ldd 4,x                 ; Map entry pointer
        addd #2                 ; Increment past the next entry pointer bytes
        std 4,x
        bne .entry_match_loop

.no_match:
        drop2                   ; Drop search string pointer
        drop2                   ; Drop next entry pointer
        drop2                   ; Drop map entry pointer
        pul b
        pul a
        pulx                    ; Return address
        drop2                   ; Drop user data on stack
        pshx                    ; Put return address back
        clc                     ; Clear carry to indicate no match
        rts

        ENDSECTION

        endif
