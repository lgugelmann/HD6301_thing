        cpu 6301

        include ../stdlib.inc

        SECTION snake
        PUBLIC snake_start

WIDTH = 40
HEIGHT = 39

ESC = $1b
CSI = "\x1b["
csi0:
        byt CSI
        byt 0
save_terminal_state:
        byt CSI
        byt "s\0"
restore_terminal_state:
        byt CSI
        byt "u\0"
; TODO: does not seem to be doing anything useful
hide_cursor:
        byt ESC
        byt "6p"
cursor_top_left:
        byt CSI
        byt "1;1H\0"
cursor_parked:
        byt CSI
        byt "41;H\0"
cursor_to_score:
        byt CSI
        byt ";38H\0"
snake_header:
        ;    1...5...10...15...20...25...30...35...40
        byt "~~TERMINAL SNEK~~~~~~~~~~~~~ Score:     \n\0"

        zp_var head_ptr,2
        zp_var head_row,1
        zp_var head_col,1
        zp_var tail_ptr,2
        zp_var tail_row,1
        zp_var tail_col,1
        zp_var last_key,1
        zp_var cur_key,1
        zp_var score,1
        reserve_memory field,WIDTH*HEIGHT
FIELD_END = field + WIDTH*HEIGHT-1

EMPTY      = %00000000
WALL       = %10000000
SNAKE      = %01000000
FOOD       = %00100000
NEXT_UP    = %00001000
NEXT_DOWN  = %00000100
NEXT_LEFT  = %00000010
NEXT_RIGHT = %00000001
DIR_MASK   = %00001111


snake_start:
        clr GRAPHICS_CLEAR
        ldx #save_terminal_state
        jsr serial_send_string

        ldx #hide_cursor
        jsr serial_send_string

        jsr serial_clear_screen

        jsr init_field

        jsr draw_game_field

        clr score
        jsr draw_score

        jsr game_loop

        ldx #restore_terminal_state
        jsr serial_send_string
        rts

game_loop:
        jsr draw_game_state
        ldx #cursor_parked
        jsr serial_send_string
        jsr draw_score
        clr timer_ticks
        lda last_key
        sta cur_key

.key_wait:
        jsr getchar
        bne .got_key
        lda timer_ticks
        cmp a,#1
        bhi .run
        bra .key_wait

.got_key:
        cmp a,#127
        bls .key_wait           ; Ignore non-control characters
        sta cur_key
        bra .key_wait

.run:
        lda cur_key
        cmp a,#KEY_UP
        bne +

        ; If the last key was down, we ignore an up as we can't go backward.
        ldb last_key
        cmp b,#KEY_DOWN
        beq game_loop
        sta last_key

        ; Mark on the current head where the next snake piece is going to be.
        ldx head_ptr
        oim #NEXT_UP,0,x
        ; Move the snake head in the new direction.
        ldd head_ptr
        subd #WIDTH
        std head_ptr
        ; Mark the new location as a snake piece.
        xgdx
        oim #SNAKE,0,x
        ; Adjust the location in row/col space too.
        dec head_row
        ; Validate that the new position is valid, drag the tail along, etc.
        jsr adjust_and_readraw_snake
        bcc game_loop
        bra .end
+
        cmp a,#KEY_DOWN
        bne +

        ldb last_key
        cmp b,#KEY_UP
        beq game_loop
        sta last_key

        ldx head_ptr
        oim #NEXT_DOWN,0,x
        ldd head_ptr
        addd #WIDTH
        std head_ptr
        xgdx
        oim #SNAKE,0,x
        inc head_row
        jsr adjust_and_readraw_snake
        bcc game_loop
        bra .end
.game_loop_jmp:
        bra game_loop
+
        cmp a,#KEY_LEFT
        bne +

        ldb last_key
        cmp b,#KEY_RIGHT
        beq .game_loop_jmp
        sta last_key

        ldx head_ptr
        oim #NEXT_LEFT,0,x
        dex
        stx head_ptr
        oim #SNAKE,0,x
        dec head_col
        jsr adjust_and_readraw_snake
        bcc .game_loop_jmp
        bra .end
+
        cmp a,#KEY_RIGHT
        bne +

        ldb last_key
        cmp b,#KEY_LEFT
        beq .game_loop_jmp
        sta last_key

        ldx head_ptr
        oim #NEXT_RIGHT,0,x
        inx
        stx head_ptr
        oim #SNAKE,0,x
        inc head_col
        jsr adjust_and_readraw_snake
        bcc .game_loop_jmp
        bra .end
+
        cmp a,"x"
        bne .game_loop_jmp

.end:
        rts

; head_ptr was moved to a new location. We need to figure out whether that has
; any consequences and drag the tail along. If the game is over sets C.
adjust_and_readraw_snake:
        ; Detect self collision: the new head already has a bit saying where the
        ; next segment is.
        ldx head_ptr
        lda 0,x
        bit a,#DIR_MASK
        bne .game_over

        ; Wall collision: the new head is also a wall.
        bit a,#WALL
        bne .game_over

        ; Draw the new head
        tab
        jsr draw_head
        tba

        ; Food collision
        bit a,#FOOD
        beq .no_food_found

        ; Hit food! Increase score, etc.
        and a,#$ff-FOOD         ; Clear the food bit
        sta 0,x
        inc score
        bra .end

.no_food_found:
        ; Adjust the tail. Figure out which direction the next snake segment is
        ; in, move the tail_ptr there.
        jsr erase_tail
        ldx tail_ptr
        lda 0,x
        clr 0,x

        bit a,#NEXT_RIGHT
        beq +
        ldd tail_ptr
        addd #1
        std tail_ptr
        inc tail_col
        bra .end
+
        bit a,#NEXT_LEFT
        beq +
        ldd tail_ptr
        subd #1
        std tail_ptr
        dec tail_col
        bra .end
+
        bit a,#NEXT_UP
        beq +
        ldd tail_ptr
        subd #WIDTH
        std tail_ptr
        dec tail_row
        bra .end
+
        bit a,#NEXT_DOWN
        beq +
        ldd tail_ptr
        addd #WIDTH
        std tail_ptr
        inc tail_row
        bra .end
.oops:
        byt "impossible!\0"
+
        ldx #.oops
        jsr serial_send_string

.game_over_string:
        byt "Game Over!\0"
.game_over:
        ldx #.game_over_string
        jsr serial_send_string
        sec                     ; Indicate game over through setting C
.end:                           ; Branching here directly keeps C clear. None of
                                ; the sums / subtractions is expected to
                                ; overflow.
        rts

; Draws the header line and the walls.
draw_game_field:
        ldx #cursor_top_left
        jsr serial_send_string

        ldx #snake_header
        jsr putstring

        ldx #field
        ldb #WIDTH
.loop:
        lda 0,x

        bne +                   ; empty spaces are zeros
        lda #" "
        jsr putchar
        bra .next
+
        bit a,#WALL
        beq +
        lda #"#"
        jsr putchar
        bra .next
+
        bit a,#SNAKE
        beq +
        lda #"x"
        jsr putchar
        bra .next
+
        bit a,#FOOD
        beq +
        lda #"O"
        jsr putchar
        bra .next
+
        lda #"?"
        jsr putchar

.next:
        cpx #FIELD_END
        beq .end
        inx
        dec b
        bne .loop
        lda #"\n"
        jsr putchar
        ldb #WIDTH
        bra .loop

.end:
        rts

; Initializes the 'field' data, placing walls and initial snake
init_field:
        ; initialize walls
        ldx #field
        jsr init_h_wall
        jsr init_v_walls
        jsr init_h_wall
        jsr place_initial_snake
        jsr place_food
        rts

init_h_wall:
        ldb #WIDTH
        lda #WALL
.loop:
        sta 0,x
        inx
        dec b
        bne .loop
        rts

; Initialize the 'vertical' walls. We have HEIGHT-2 of those.
init_v_walls:
        ldb #HEIGHT-2
.v_loop:
        psh b
        ; Put down one wall
        lda #WALL
        sta 0,x
        inx

        ; Next WIDTH-2 empty squares
        lda #EMPTY
        ldb #WIDTH-2
.h_loop:
        sta 0,x
        inx
        dec b
        bne .h_loop

        ; Put down another wall
        lda #WALL
        sta 0,x
        inx

        pul b
        dec b
        bne .v_loop

        rts

place_initial_snake:
        ; Want the snake to be pointing right, about 1/4 of the way
        ; horizontally, and in the middle vertically. It has length 2.
        ldx #field + WIDTH*(HEIGHT/2) + WIDTH/4
        stx head_ptr
        lda #SNAKE
        sta 0,x
        dex
        stx tail_ptr
        ora #NEXT_RIGHT
        sta 0,x

        ; +1 because rows are 1-based, and another +1 for the header row
        lda #HEIGHT/2 + 1 + 1
        sta head_row
        sta tail_row

        lda #WIDTH/4 + 1
        sta head_col
        dec a
        sta tail_col

        lda #KEY_RIGHT
        sta last_key
        rts

place_food:
        ldx #field + WIDTH*(HEIGHT/2) + 3*WIDTH/4
        lda #FOOD
        sta 0,x
        rts

; Draw the snake head.
draw_head:
        lda head_row
        jsr set_cursor_vertical
        lda head_col
        jsr set_cursor_horizontal
        lda #"x"
        jsr serial_send_byte
        rts

; Erase the tail at its current location.
erase_tail:
        lda tail_row
        jsr set_cursor_vertical
        lda tail_col
        jsr set_cursor_horizontal
        lda #" "
        jsr serial_send_byte
        rts

; Puts the score onto the screen
draw_score:
        ldx #cursor_to_score
        jsr serial_send_string
        lda score
        jsr putchar_dec
        rts

draw_game_state:
        psh a
        pshx

        ldx #cursor_parked
        jsr serial_send_string

        ldx #.state_string
        jsr putstring

        ldx head_ptr
        jsr putx_hex

        lda #" "
        jsr putchar
        lda 0,x
        jsr putchar_hex

        lda #" "
        jsr putchar
        lda head_col
        jsr putchar_dec

        lda #" "
        jsr putchar
        lda head_row
        jsr putchar_dec

        lda #" "
        jsr putchar
        ldx tail_ptr
        jsr putx_hex

        lda #" "
        jsr putchar
        lda 0,x
        jsr putchar_hex

        lda #" "
        jsr putchar
        lda tail_col
        jsr putchar_dec

        lda #" "
        jsr putchar
        lda tail_row
        jsr putchar_dec

        pulx
        pul a
        rts
.state_string:
        byt "  HP  H  HC  HR   TP  T  TC  TR\n\0"

; Move the cursor to column set by A. Clobbers X.
set_cursor_horizontal:
        ldx #csi0
        jsr serial_send_string
        jsr putchar_dec
        lda #'G'
        jsr serial_send_byte
        rts

set_cursor_vertical:
        ldx #csi0
        jsr serial_send_string
        jsr putchar_dec
        lda #'d'
        jsr serial_send_byte
        rts

        ENDSECTION
