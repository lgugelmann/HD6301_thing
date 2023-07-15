;
; Seq, a super-simple sequencer
;
; Keybindings:
; * left / right / up /down navigate through notes
; * a-f, 1-8 to set note and octave
; * ',' flat '.' natural '/' sharp
; * 'x' to clear note
; * space to start / stop playing

; TODOs / Improvement ideas:
; - General UI niceties: title, keybinding on screen etc.
; - Selectable tempo
; - More channels
; - Selectable instruments
; - Per-channel volume
; - Per-channel play cursor + 'r'epeat + 's'top markers
; - Play cursor indicated by color to save space
; - Per-channel mute
; - More bars per channel (wrapping or scrolling)
; - Load / save support
; - Instrument editor
; - Different amount of notes per bar
; - Selectable number of bars

        cpu 6301

        include ../stdlib.inc

        SECTION seq
        PUBLIC seq_start


TICK_INTERVAL = 4               ; How many timer ticks between notes
CHANNELS = 4                    ; How many channels we have

BARS = 8                        ; Number of bars in a line on screen
NOTES_PER_BAR = 4               ; Number of notes per bar
CHARS_PER_NOTE = 3              ; The number of characters between 2 notes
FIRST_NOTE_OFFSET = 1           ; The offset into 'notes' for the first note
; The offset into 'notes' for the last note
LAST_NOTE_OFFSET = FIRST_NOTE_OFFSET + (BARS*NOTES_PER_BAR-1)*CHARS_PER_NOTE
; The length in characters of a 'notes' line +1 for the 0 byte at the end
NOTES_LENGTH = FIRST_NOTE_OFFSET+BARS*NOTES_PER_BAR*CHARS_PER_NOTE+1

; The screen column at which we start drawing 'notes' strings
NOTES_COLUMN = 1
; The screen column for the first note
FIRST_NOTE_COLUMN = NOTES_COLUMN + FIRST_NOTE_OFFSET
; The screen column for the last note
LAST_NOTE_COLUMN = NOTES_COLUMN + LAST_NOTE_OFFSET

FIRST_NOTES_ROW = 3              ; The first row we display notes in
PLAY_CURSOR_ROW = FIRST_NOTES_ROW + CHANNELS

        if FIRST_NOTE_COLUMN + NOTES_LENGTH > 100
          error "This won't fit on the screen!"
        endif

        ; One notes buffer per channel to store the notes to play. It does
        ; double-duty as both the on-scren string and the buffer we play from.
        reserve_memory notes, NOTES_LENGTH*CHANNELS
        ; The color for each character of one channel in 'notes'
        reserve_memory note_colors, NOTES_LENGTH
        zp_var play_pos, 1      ; Offset in a 'notes' channel for the next note
        zp_var cursor_row, 1    ; Edit cursor row position
        zp_var cursor_column, 1 ; Edit cursor column position
        zp_var playing, 1       ; 0 not playing, 1 playing

seq_start:
        clr GRAPHICS_CLEAR
        clr playing
        lda #FIRST_NOTE_COLUMN
        sta cursor_column
        lda #FIRST_NOTES_ROW
        sta cursor_row

        jsr init_instrument
        jsr init_notes
        jsr init_note_colors

        jsr draw_notes          ; Draws text & colors

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

; Init the notes buffers
init_notes:
        ldx #notes

        ldb #CHANNELS
.channel_loop:
        psh b

        lda #'|'
        sta 0,x
        inx

        lda #BARS
.bar_loop:
        psh a
        lda #NOTES_PER_BAR
.note_loop:
        ldb #'x'
        stb 0,x
        ldb #'-'
        stb 1,x
        stb 2,x
        ldb #CHARS_PER_NOTE
        abx
        dec a
        bne .note_loop
        pul a
        dec a
        bne .bar_loop

        clr 0,x                 ; Put a 0 at the end of the buffer

        inx                     ; We're now in the next channel buffer
        pul b
        dec b
        bne .channel_loop

        rts

; Initialize the note_colors string
init_note_colors:
        ; Init everything with black at first
        ldx #note_colors
        lda #COLOR_BLACK | COLOR_BG
.loop:
        sta 0,x
        inx
        cpx #note_colors + NOTES_LENGTH - 1
        bne .loop

        ; 0-terminate the string. Every other entry has the BG bit set so this
        ; is the only 0 in the string.
        clr a
        sta 0,x

        ; Color the column before the first note in a bar
        ldx #note_colors
        lda #COLOR_LIGHT_GREY | COLOR_BG
        ldb #NOTES_PER_BAR*CHARS_PER_NOTE
.bar_loop:
        sta 0,x
        abx
        cpx #note_colors + NOTES_LENGTH - 1
        blt .bar_loop

        rts

; Draws the notes strings and set the background as well
draw_notes:
        ldx #notes
        lda #FIRST_NOTES_ROW
.notes_loop:
        sta GRAPHICS_SET_ROW
        ldb #NOTES_COLUMN
        stb GRAPHICS_SET_COLUMN
        jsr putstring           ; Post: x is at buffer-terminating 0
        inx                     ; Move to next notes buffer
        inc a
        cmp a,#FIRST_NOTES_ROW + CHANNELS
        bne .notes_loop

        ldb #FIRST_NOTES_ROW
.color_loop:
        stb GRAPHICS_SET_ROW
        psh b
        ldb #NOTES_COLUMN
        stb GRAPHICS_SET_COLUMN
        ldx #note_colors
        ldb #1
        lda 0,x
.loop:
        sta GRAPHICS_SET_COLOR
        stb GRAPHICS_MOVE_CURSOR
        inx
        lda 0,x
        bne .loop               ; note_colors is 0-terminated

        pul b
        inc b
        cmp b,#FIRST_NOTES_ROW + CHANNELS
        bne .color_loop

        rts

; Redraw the piece of the notes string that could just have changed
refresh_notes:
        ; If anything changed, it's in the NOTES_LENGTH many characters to the
        ; right of X. The cursor not necessarily already in the right place so
        ; we need to set that too.
        lda 0,x
        sta GRAPHICS_SEND_CHAR
        lda 1,x
        sta GRAPHICS_SEND_CHAR
        lda 2,x
        sta GRAPHICS_SEND_CHAR
        lda #-3
        sta GRAPHICS_MOVE_CURSOR
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
        cmp a,#'-'
        bne .octave
        lda #'_'                ; sharp/flat is unset, use '_' as default
        sta 1,x

.octave:
        lda 2,x
        cmp a,#'-'
        bne .end
        lda #'4'                ; octave is unset, use 4 as default
        sta 2,x

.end:
        rts

edit_loop:
        ; Place the cursor at the first note and point X there too
        lda #FIRST_NOTES_ROW
        sta cursor_row

        lda #FIRST_NOTE_COLUMN
        sta cursor_column

        ldx #notes + FIRST_NOTE_OFFSET

.refresh_cursor:
        lda cursor_row
        sta GRAPHICS_SET_ROW
        lda cursor_column
        sta GRAPHICS_SET_COLUMN
        bra .charloop
.refresh_notes:
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
        lda #'-'
        sta 1,x
        sta 2,x
        bra .refresh_notes
+
        cmp a,#'a'
        blt +                   ; below 'a'
        cmp a,#'g'
        bgt .charloop           ; above 'g' - invalid command

        sta 0,x
        jsr update_defaults
        bra .refresh_notes
+
        cmp a,#'1'
        blt +                   ; below '1'
        cmp a,#'8'
        bgt .charloop           ; above '8' - invalid command

        sta 2,x
        jsr update_defaults
        bra .refresh_notes
+
        cmp a,#','              ; Command for 'flat'
        bne +
        lda #'b'
        sta 1,x
        jsr update_defaults
        bra .refresh_notes
+
        cmp a,#'.'              ; Command for 'natural'
        bne +
        lda #'_'
        sta 1,x
        jsr update_defaults
        bra .refresh_notes
+
        cmp a,#'/'              ; Command for 'sharp'
        bne +
        lda #'#'
        sta 1,x
        jsr update_defaults
        bra .refresh_notes
+
        cmp a,#KEY_RIGHT
        bne +
        lda cursor_column
        cmp a,#LAST_NOTE_COLUMN ; Are we all the way at the right?
        beq .charloop           ; yes, ignore
        ldb #CHARS_PER_NOTE
        abx
        aba
        sta cursor_column
        jmp .refresh_cursor

.charloop_jmp:                  ; workaround for jump distance too big
        jmp .charloop

+
        cmp a,#KEY_LEFT
        bne +
        ldb cursor_column
        cmp b,#FIRST_NOTE_COLUMN ; Are we all the way at the left?
        beq .charloop_jmp        ; if yes, ignore
        xgdx                     ; X has no sub instruction, go via D
        subd #CHARS_PER_NOTE
        xgdx
        sub b,#CHARS_PER_NOTE
        stb cursor_column
        jmp .refresh_cursor
+
        cmp a,#KEY_UP
        bne +
        lda cursor_row
        cmp a,#FIRST_NOTES_ROW
        beq .charloop_jmp       ; Branch if already at the first row
        dec a
        sta cursor_row
        ; There is no decrement for X, so we need to do it in D
        xgdx
        subd #NOTES_LENGTH
        xgdx
        jmp .refresh_cursor
+
        cmp a,#KEY_DOWN
        bne +
        lda cursor_row
        cmp a,#FIRST_NOTES_ROW+CHANNELS-1
        beq .charloop_jmp       ; If at the last row already, nothing to do
        inc a
        sta cursor_row
        ldb #NOTES_LENGTH
        abx
        jmp .refresh_cursor
+
        cmp a,#' '
        bne .charloop_jmp
        lda #FIRST_NOTE_OFFSET  ; Reset playing position
        sta play_pos
        clr timer_ticks
        eim #1,playing          ; Start / stop playing
        beq .stop_playing       ; If we're stopping, stop
        jmp .charloop
.stop_playing:
        jsr stop
        jmp .charloop

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
        lda #FIRST_NOTE_COLUMN
        sta GRAPHICS_MOVE_CURSOR

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

        ldb #NOTES_LENGTH
        abx                     ; Move to the next notes string

        inc a
        cmp a,#CHANNELS+1       ; +1 because channels in OPL3 are 1-based
        bne .channel_loop

        ldb play_pos
        add b,#CHARS_PER_NOTE
        cmp b,#LAST_NOTE_OFFSET
        ble .end2               ; branch if not past the end
        ldb #FIRST_NOTE_OFFSET
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

; Stop playing notes on all channels and clear the play cursor
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
