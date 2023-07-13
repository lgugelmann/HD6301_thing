;
; Seq, a super-simple sequencer
;
; Keybindings:
; * <- / -> navigate through notes
; * a-f, 1-8 to set note and octave
; * ',' flat '.' natural '/' sharp
; * space to start playing

        cpu 6301

        include ../stdlib.inc

        SECTION seq
        PUBLIC seq_start

        reserve_memory notes, 100
        zp_var cursor_column, 1
        zp_var playing, 1
        zp_var play_pos, 2

LAST_NOTE = 93                  ; Position of the first char of the last note
TICK_INTERVAL = 4

seq_start:
        clr GRAPHICS_CLEAR
        clr playing
        clr cursor_column

        jsr init_instrument
        jsr init_notes
        jsr edit_loop
        rts

init_instrument:
        lda #1                  ; Channel number
        ldx #instruments_piano
        jsr sound_load_instrument
        jsr sound_stop_note
        rts

init_notes:
        ldx #notes
        lda #6

        inx
.bar_loop:
        dex
        ldb #'|'
        stb 0,x
        inx

        psh a
        lda #4
.note_loop:
        ldb #'x'
        stb 0,x
        stb 1,x
        stb 2,x
        ldb #'-'
        stb 3,x
        ldb #4
        abx
        dec a
        bne .note_loop
        pul a
        dec a
        bne .bar_loop

        dex
        lda #'|'
        sta 0,x
        clr 1,x

        rts

; Redraw the notes string on the screen
refresh_notes:
        pshx
        psh a

        clr GRAPHICS_SET_COLUMN
        ldx #notes

        lda #1
        sta GRAPHICS_HIDE_CURSOR ; Cursor off to make putstring look better
        jsr putstring
        clr GRAPHICS_HIDE_CURSOR ; Cursor back on

        lda cursor_column
        sta GRAPHICS_SET_COLUMN

        pul a
        pulx
        rts

; If a note goes from unset to something, set default values. X points to the
; start of the triple of characters that define a note.
update_defaults:
        lda 0,x
        cmp a,#'x'
        bne .sharps
        lda #'a'                ; Note is unset, use 'A' as default
        sta 0,x
.sharps:
        lda 1,x
        cmp a,#'x'
        bne .octave
        lda #'_'                ; sharp/flat is unset, use '_' as default
        sta 1,x

.octave:
        lda 2,x
        cmp a,#'x'
        bne .end
        lda #'4'                ; octave is unset, use 4 as default
        sta 2,x

.end:
        rts

edit_loop:
        ; Place the cursor at the first note and point X there too
        lda #1
        sta cursor_column
        sta GRAPHICS_SET_COLUMN
        ldx #notes+1

.next:
        jsr refresh_notes
.charloop:
        lda playing
        beq .chr
        lda timer_ticks
        cmp a,#TICK_INTERVAL
        blt .chr
        clr timer_ticks
        jsr play_next
.chr:
        jsr getchar
        beq .charloop

        jsr stop

        cmp a,#'x'
        bne +
        sta 0,x
        sta 1,x
        sta 2,x
        bra .next
+
        cmp a,#'a'
        blt +                   ; below 'a'
        cmp a,#'g'
        bgt .charloop           ; above 'g' - invalid command

        sta 0,x
        jsr update_defaults
        bra .next
+
        cmp a,#'1'
        blt +                   ; below '1'
        cmp a,#'8'
        bgt .charloop           ; above '8' - invalid command

        sta 2,x
        jsr update_defaults
        bra .next
+
        cmp a,#','              ; Command for 'flat'
        bne +
        lda #'b'
        sta 1,x
        jsr update_defaults
        bra .next
+
        cmp a,#'.'              ; Command for 'natural'
        bne +
        lda #'_'
        sta 1,x
        jsr update_defaults
        bra .next
+
        cmp a,#'/'              ; Command for 'sharp'
        bne +
        lda #'#'
        sta 1,x
        jsr update_defaults
        bra .next
+
        cmp a,#KEY_RIGHT
        bne +
        cpx #notes+LAST_NOTE    ; Are we all the way at the right?
        beq .next               ; yes, ignore
        ldb #4
        abx
        add b,cursor_column
        stb cursor_column
        jmp .next
+
        cmp a,#KEY_LEFT
        bne +
        cpx #notes+1            ; Are we all the way at the left?
        beq .next2              ; if yes, ignore
        dex
        dex
        dex
        dex
        ldb cursor_column
        sub b,#4
        stb cursor_column
        jmp .next
+
        cmp a,#' '
        bne .next2
        ldd #notes+1            ; Reset playing position
        std play_pos
        clr timer_ticks
        inc playing             ; playing is 0, set it to 1
        jmp .charloop
.next2:
        jmp .next
        rts

; When called play the note corresponding to the 3 note characters at 'play_pos'
; and increase play_pos (or reset to the start).
play_next:
        pshx
        ldx play_pos

        lda 0,x
        cmp a,#'x'
        beq .end

        ; Octave to MIDI note number: C1 -> 24, C2 -> 36, so (octave#+1)*12
        lda 2,x                 ; octave number as ASCII digit
        sub a,#'0'-1            ; sub '0'-1 to get the +1 we need
        ldb #12
        mul                     ; low order byte (what we need) ends up in B

        lda 0,x                 ; note name
        ; Offset: C=0, D=2, E=4, F=5, G=7, A=9, B=11
        cmp a,#'d'
        bne +
        lda #2
        bra .sharp
+
        cmp a,#'e'
        bne +
        lda #4
        bra .sharp
+
        cmp a,#'f'
        bne +
        lda #5
        bra .sharp
+
        cmp a,#'g'
        bne +
        lda #7
        bra .sharp
+
        cmp a,#'a'
        bne +
        lda #9
        bra .sharp
+
        cmp a,#'b'
        bne +
        lda #11
        bra .sharp
+
        lda #0

.sharp:
        aba                     ; Finish adding the note delta from above
        tab                     ; aba ends up in A, we need it in B

        lda 1,x                 ; Load flat/sharp marker

        cmp a,#'b'
        bne +
        dec b
        bra .play
+
        cmp a,#'#'
        bne .play
        inc b

.play:
        tba

        lda #1                  ; Channel number, B contains the MIDI note
        jsr sound_play_note

.end:
        ldb #4
        abx
        cpx #notes+LAST_NOTE
        ble .end2               ; branch if not past the end
        ldx #notes+1
.end2:
        stx play_pos
        pulx
        rts

stop:
        clr playing
        psh a
        lda #1                  ; Channel number
        jsr sound_stop_note
        pul a
        rts

        ENDSECTION
