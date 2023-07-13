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

NOTE_LENGTH = 98                ; How many characters a 'notes' line is
LAST_NOTE = 93                  ; Position of the first char of the last note
TICK_INTERVAL = 4
CHANNELS = 4
PLAY_CURSOR_ROW = CHANNELS

        reserve_memory notes, NOTE_LENGTH*CHANNELS
        zp_var play_pos, 1      ; Offset into 'notes' for the next note
        zp_var cursor_row, 1    ; Edit cursor row position
        zp_var cursor_column, 1 ; Edit cursor column position
        zp_var playing, 1       ; 0 not playing, 1 playing

seq_start:
        clr GRAPHICS_CLEAR
        clr playing
        clr cursor_row
        clr cursor_column

        jsr init_instrument

        ldx #notes
        lda #CHANNELS
        ldb #NOTE_LENGTH
.loop:
        jsr init_notes
        abx                     ; move to next 'notes' line
        dec a
        bne .loop

        jsr edit_loop
        rts

init_instrument:
        lda #1                  ; Channel number
        ldx #instruments_piano
        jsr sound_load_instrument
        jsr sound_stop_note

        lda #2                  ; Channel number
        ldx #instruments_piano
        jsr sound_load_instrument
        jsr sound_stop_note

        lda #3                  ; Channel number
        ldx #instruments_piano
        jsr sound_load_instrument
        jsr sound_stop_note

        lda #4                  ; Channel number
        ldx #instruments_tsch
        jsr sound_load_instrument
        jsr sound_stop_note
        rts

; Init the notes buffer in X
init_notes:
        pshx
        psh a
        psh b

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

        pul b
        pul a
        pulx
        rts

; Redraw the notes string on the screen
refresh_notes:
        pshx

        clr a
        ldx #notes
.loop:
        clr b
        stb GRAPHICS_SET_COLUMN
        sta GRAPHICS_SET_ROW

        ldb #1
        stb GRAPHICS_HIDE_CURSOR ; Cursor off to make putstring look better
        jsr putstring            ; X now points to the 0 at the end of 'notesX'
        clr GRAPHICS_HIDE_CURSOR ; Cursor back on

        ; Set X to the start of the next 'notes' string
        inx

        inc a
        cmp a,#CHANNELS
        bne .loop

        ; Set cursor to the edit row / column
        lda cursor_row
        sta GRAPHICS_SET_ROW
        lda cursor_column
        sta GRAPHICS_SET_COLUMN

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
        lda cursor_column
        cmp a,#LAST_NOTE        ; Are we all the way at the right?
        beq .next               ; yes, ignore
        ldb #4
        abx
        aba
        sta cursor_column
        jmp .next
+
        cmp a,#KEY_LEFT
        bne +
        ldb cursor_column
        cmp b,#1                ; Are we all the way at the left?
        beq .next2              ; if yes, ignore
        dex
        dex
        dex
        dex
        sub b,#4
        stb cursor_column
        jmp .next
+
        cmp a,#KEY_UP
        bne +
        lda cursor_row
        beq .next2              ; If 0, nothing to do
        dec a
        sta cursor_row
        ; We don't have a way to decrement X easily, so we need to sum up
        ldx #notes
        ldb cursor_column
        abx
        ldb #NOTE_LENGTH
.key_up_add_loop:
        tst a
        beq .next2
        abx
        dec a
        bra .key_up_add_loop
+
        cmp a,#KEY_DOWN
        bne +
        lda cursor_row
        cmp a,#CHANNELS-1
        beq .next2              ; If at the last row already, nothing to do
        inc a
        sta cursor_row
        ldb #NOTE_LENGTH
        abx
        jmp .next
+
        cmp a,#' '
        bne .next2
        lda #1                  ; Reset playing position
        sta play_pos
        clr timer_ticks
        eim #1,playing          ; Start / stop playing
        beq .stop_playing       ; If we're stopping, stop
        jmp .charloop
.stop_playing:
        jsr stop
.next2:
        jmp .next
        rts

; When called play the note corresponding to the 3 note characters at 'play_pos'
; and increase play_pos (or reset to the start).
play_next:
        pshx

        ; Show a caret under the note played
        lda #PLAY_CURSOR_ROW
        sta GRAPHICS_SET_ROW
        ldb #1
        stb GRAPHICS_CLEAR

        ; Turns out that the graphics clear is really expensive and we lose the
        ; next row/column operations if we don't nop for a bit.
        nop10
        nop10

        sta GRAPHICS_SET_ROW
        ldb play_pos
        stb GRAPHICS_SET_COLUMN

        lda #'^'
        sta GRAPHICS_SEND_CHAR_RAW

        ; Set the cursor back to the editing position
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        lda cursor_row
        sta GRAPHICS_SET_ROW

        ; Set up X to point to the next note
        ldx #notes
        abx                     ; B is still play_pos
        lda #1                  ; First channel number
.channel_loop:
        jsr play_next_channel

        ldb #NOTE_LENGTH
        abx                     ; Move to the next notes string

        inc a
        cmp a,#CHANNELS+1       ; +1 because channels in OPL3 are 1-based
        bne .channel_loop

        ldb play_pos
        add b,#4
        cmp b,#LAST_NOTE
        ble .end2               ; branch if not past the end
        ldb #1
.end2:
        stb play_pos

        pulx
        rts

; Play the note X points to on the channel in A
play_next_channel:
        psh a
        lda 0,x
        cmp a,#'x'
        beq .end

        ; Octave to MIDI note number: C1 -> 24, C2 -> 36, so (octave#+1)*12
        lda 2,x                 ; octave number as ASCII digit
        sub a,#'0'-1            ; sub '0'-1 to get the +1 in (octave#1+1)
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
        clr a

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
        pul a                  ; Channel number, B contains the MIDI note
        jsr sound_play_note
        bra .end2

.end:
        pul a
.end2:
        rts

stop:
        psh a
        lda #CHANNELS
.loop:
        jsr sound_stop_note
        dec a
        bne .loop

        ; Clear the row with the play caret
        lda #PLAY_CURSOR_ROW
        sta GRAPHICS_SET_ROW
        lda #1
        sta GRAPHICS_CLEAR      ; 1: clear row, reset cursor column to start

        nop10
        nop10

        ; Reset cursor to edit row
        lda cursor_row
        sta GRAPHICS_SET_ROW
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        pul a
        rts

        ENDSECTION
