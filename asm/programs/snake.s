        cpu 6301

        include ../stdlib.inc

        SECTION snake
        PUBLIC snake_start

WIDTH = 40
HEIGHT = 20
FIELD_SIZE = WIDTH*HEIGHT
; To generate random field positions it's useful to know how many bits from the
; RNG we can truncate to.
WIDTH_MASK = 2^LASTBIT(WIDTH)-1
HEIGHT_MASK = 2^LASTBIT(HEIGHT)-1

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
snake_header:
        ;    1...5...10...15...20...25...30...35...40
        byt "~~TERMINAL SNEK~~~~~~~~~~~~~ Score:     \n\0"
cursor_parked:
        byt CSI
        byt "41;H\0"

        zp_var head_ptr,2
        zp_var head_row,1
        zp_var head_col,1
        zp_var tail_ptr,2
        zp_var tail_row,1
        zp_var tail_col,1
        zp_var last_key,1
        zp_var cur_key,1
        zp_var score,1
        reserve_memory field,FIELD_SIZE
FIELD_END = field + FIELD_SIZE-1

; The values in the game field above. The top 3 bits describe the type of item
; that's there. The bottom 4 are only set on 'SNAKE' fields and give the
; direction of where the next bit of snake is. This lets us drag the tail in the
; right direction without having to store large pointers. The head has no
; direction marker.
EMPTY      = %00000000
WALL       = %10000000
SNAKE      = %01000000
FOOD       = %00100000
NEXT_UP    = %00001000
NEXT_DOWN  = %00000100
NEXT_LEFT  = %00000010
NEXT_RIGHT = %00000001
DIR_MASK   = %00001111

FOOD_SYMBOL  = "o"
WALL_SYMBOL  = "#"
SNAKE_SYMBOL = "x"

snake_start:
        ldx #save_terminal_state
        jsr serial_send_string

        jsr clear_screen
        jsr set_cursor_hidden

        ; Seed random number generator with low counter byte
        lda TCL
        jsr random_srand

.warm_start:
        jsr init_field
        jsr draw_game_field

        clr score
        jsr draw_score

        ; Wait for the press of any button to start the game
.wait_start:
        jsr getchar
        beq .wait_start

        jsr game_loop

        ; Wait 200ms and drain leftover keypresses from the heat of the game.
        clr timer_ticks
.wait:
        jsr getchar
        lda timer_ticks
        cmp a,#4
        bmi .wait

        ; Now wait for an actual keypress
.wait_end:
        jsr getchar
        beq .wait_end

        ; Start another game if 'r' was pressed, quit otherwise
        cmp a,#"r"
        beq .warm_start

        jsr clear_screen

        ldx #restore_terminal_state
        jsr serial_send_string
        rts

game_loop:
        jsr draw_score

        ; Make sure the cursor is out of the way
        ldx #cursor_parked
        jsr serial_send_string

        ; Zero out the timer
        clr timer_ticks

        ; If no key is pressed we want to continue in the previous direction
        lda last_key
        sta cur_key

.tick_wait:
        jsr get_keypress
        lda timer_ticks
        cmp a,#2
        bmi .tick_wait

.run:
        ldx head_ptr            ; Prepare X with the current head

        lda cur_key
        sta last_key

        cmp a,#KEY_UP
        bne +

        ; Mark on the current head position where the new head is going to be.
        oim #NEXT_UP,0,x
        lda head_row
        dec a                   ; Moving up, so lower in number of rows
        sta head_row
        ldb head_col
        bra .move_snake
+
        cmp a,#KEY_DOWN
        bne +

        oim #NEXT_DOWN,0,x
        lda head_row
        inc a                   ; Moving down, so higher in number of rows
        sta head_row
        ldb head_col
        bra .move_snake
+
        cmp a,#KEY_LEFT
        bne +

        oim #NEXT_LEFT,0,x
        lda head_row
        ldb head_col
        dec b                   ; Moving left, so lower in number of columns
        stb head_col
        bra .move_snake
+
        cmp a,#KEY_RIGHT
        bne +

        oim #NEXT_RIGHT,0,x
        lda head_row
        ldb head_col
        inc b                   ; Moving right, so higher in number of columns
        stb head_col

.move_snake:
        ; Turn row/col into a pointer into field in X
        jsr row_col_to_field_ptr
        stx head_ptr
        ; Mark the new head location as a piece of the snake
        oim #SNAKE,0,x
        ; Validate that the new position is valid, drag the tail along, etc.
        jsr adjust_and_readraw_snake
        bcc game_loop           ; Carry is clear unless the game is over
.end:
        rts

; Collect key presses, validate them, and store them in cur_key if valid.
get_keypress:
        jsr getchar
        beq .end                ; No keypress, return

        cmp a,#127
        bls .end                ; Ignore non-control characters

        ; We want to ignore keys that go 'backward' vs the current direction. So
        ; if we're going up, then 'down' is not allowed. Instead of doing 4
        ; cases we're doing a bit of trickery to convert a key to its 'inverse'
        ; by exploiting the values they have. UP=$82, DOWN=$83, LEFT=$80,
        ; RIGHT=$81 - so increment even numbers, decrement odd ones.
        tab                     ; Tuck away the non-modified version
        bit a,#$01
        bne .odd_key_value
        inc a
        bra .compare
.odd_key_value:
        dec a

.compare:
        cmp a,last_key
        beq .end
        tba                     ; Get the unmodified key back
        sta cur_key

.end:
        rts

; Takes row in A, column in B, and computes the corresponding pointer into
; 'field' which is returned in X.
row_col_to_field_ptr:
        dec a                   ; row is 1-based
        dec a                   ; ...and we have a header to take into account
        dec b                   ; col is 1-based

        psh b                   ; Store the column
        ldb #WIDTH
        mul                     ; A*B -> (A|B)
        addd #field             ; make it a pointer into field
        xgdx                    ; Move D into X
        pul b
        abx                     ; Add B to X
        rts


; head_ptr was moved to a new location. We need to figure out whether that has
; any consequences and drag the tail along. If the game is over sets C.
adjust_and_readraw_snake:
        ; Detect self collision: the new head already has a bit saying where the
        ; next segment is. Heads are not supposed to have direction bits.
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
        jsr place_and_draw_food
        bra .end

.no_food_found:
        ; Adjust the tail. Figure out which direction the next snake segment is
        ; in, move the tail_ptr there.
        jsr erase_tail          ; This takes care of the drawing
        ldx tail_ptr
        lda 0,x
        clr 0,x                 ; Clear the tail position from 'field' too

        ; Follow from the current tail to where the next one will be and adjust
        ; row/col/ptr.
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
        ldd tail_ptr
        addd #WIDTH
        std tail_ptr
        inc tail_row
        bra .end

.game_over:
        jsr draw_game_over_message
        sec                     ; Indicate game over through setting C
.end:
        ; Branching here directly keeps C clear. None of the sums / subtractions
        ; is expected to overflow.
        rts

draw_game_over_message:
        lda #10
        jsr set_cursor_horizontal
        lda #6
        jsr set_cursor_vertical
        ldx #.game_over_string_0
        jsr putstring

        lda #10
        jsr set_cursor_horizontal
        lda #7
        jsr set_cursor_vertical
        ldx #.game_over_string_1
        jsr putstring

        lda #10
        jsr set_cursor_horizontal
        lda #8
        jsr set_cursor_vertical
        ldx #.game_over_string_2
        jsr putstring

        lda #10
        jsr set_cursor_horizontal
        lda #9
        jsr set_cursor_vertical
        ldx #.game_over_string_1
        jsr putstring

        lda #10
        jsr set_cursor_horizontal
        lda #10
        jsr set_cursor_vertical
        ldx #.game_over_string_0
        jsr putstring
        rts

.game_over_string_0:
        byt "$$$$$$$$$$$$$$$$$$$$\0"
.game_over_string_1:
        byt "$                  $\0"
.game_over_string_2:
        byt "$    Game Over!!   $\0"


; Draws the header line and the walls.
draw_game_field:
        jsr set_cursor_top_left

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
        lda #WALL_SYMBOL
        jsr putchar
        bra .next
+
        bit a,#SNAKE
        beq +
        lda #SNAKE_SYMBOL
        jsr putchar
        bra .next
+
        bit a,#FOOD
        beq +
        lda #FOOD_SYMBOL
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
        jsr place_and_draw_food
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
        ; +1 because rows are 1-based, and another +1 for the header row
        lda #HEIGHT/2 + 1 + 1
        sta head_row
        sta tail_row

        ldb #WIDTH/4 + 1
        stb head_col
        dec b
        stb tail_col

        jsr row_col_to_field_ptr
        stx tail_ptr

        lda #SNAKE + NEXT_RIGHT
        sta 0,x

        inx                     ; Head is one position ahead of the tail
        stx head_ptr
        lda a,#SNAKE
        sta 0,x

        ; The snake head is to the right of its tail, so it's moving right.
        lda #KEY_RIGHT
        sta last_key
        rts

; Place food in a random empty spot. TODO: verify that this actually mostly
; works even for tight fields.
place_and_draw_food:
        bra .column
.restart:
        ins                     ; We need to get rid of that psh a, psh b
        ins
.column:
        jsr random_byte
        and a,#WIDTH_MASK
        cmp a,#WIDTH-1
        bhi .column
        inc a                   ; columns are 1-based
        tab
.row:
        jsr random_byte
        and a,#HEIGHT_MASK
        cmp a,#HEIGHT-1
        bhi .row
        inc a
        inc a                   ; rows are 1-based and we need to count the
                                ; header too.

        psh a                   ; Store row, column
        psh b

        ; Check whether we got an empty field - if not we retry.
        jsr row_col_to_field_ptr
        lda 0,x
        bne .restart

        ; Found an empty spot. Make it food.
        lda #FOOD
        sta 0,x

        ; Draw the food
        pul a                   ; Get the column back
        jsr set_cursor_horizontal
        pul a                   ; Get the row back
        jsr set_cursor_vertical

        lda #FOOD_SYMBOL
        jsr putchar

        rts

; Draw the snake head.
draw_head:
        lda head_row
        jsr set_cursor_vertical
        lda head_col
        jsr set_cursor_horizontal
        lda #SNAKE_SYMBOL
        jmp putchar
        ; rts in putchar

; Erase the tail at its current location.
erase_tail:
        lda tail_row
        jsr set_cursor_vertical
        lda tail_col
        jsr set_cursor_horizontal
        lda #" "
        jmp putchar
        ; rts in putchar

; Puts the score onto the screen
draw_score:
        clr GRAPHICS_SET_ROW
        lda #37
        sta GRAPHICS_SET_COLUMN
        ldx #.cursor_to_score
        jsr serial_send_string
        lda score
        jmp putchar_dec
        ; rts in putchar_dec

.cursor_to_score:
        byt CSI
        byt ";38H\0"

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

set_cursor_top_left:
        clr GRAPHICS_SET_CURSOR_HIGH
        clr GRAPHICS_SET_CURSOR_LOW
        ldx #.cursor_top_left
        jmp serial_send_string

.cursor_top_left:
        byt CSI
        byt "1;1H\0"


; Move the cursor to column set by A. Clobbers X.
set_cursor_horizontal:
        dec a                   ; Graphics row is 0-based
        sta GRAPHICS_SET_COLUMN
        inc a
        ldx #csi0
        jsr serial_send_string
        jsr serial_send_byte_dec
        lda #'G'
        jmp serial_send_byte
        ; rts in serial_send_byte

; Move the cursor to row set by A. Clobbers X.
set_cursor_vertical:
        dec a                   ; Graphics row is 0-based
        sta GRAPHICS_SET_ROW
        inc a
        ldx #csi0
        jsr serial_send_string
        jsr serial_send_byte_dec
        lda #'d'
        jmp serial_send_byte
        ; rts in serial_send_byte

set_cursor_hidden:
        inc GRAPHICS_HIDE_CURSOR
        ldx #.hide_cursor
        jmp serial_send_string
        ; rts in serial_send_string

; TODO: does not seem to be doing anything useful
.hide_cursor:
        byt ESC
        byt "6p"

clear_screen:
        clr GRAPHICS_CLEAR
        jmp serial_clear_screen

        ENDSECTION
