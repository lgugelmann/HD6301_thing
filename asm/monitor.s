        cpu 6301

        org $f000

        include include/graphics
        include include/keyboard
        include include/macros
        include include/registers
        include include/serial

BUFFER_SIZE = GRAPHICS_TERMINAL_WIDTH
        ; Input buffer, holds one line
        reserve_memory buffer, BUFFER_SIZE
        reserve_memory buffer_ptr, 2
        reserve_memory command_ptr, 2

terminal_string:
        byt KEY_ENTER
        byt "> \0"
error_string:
        byt "Unknown command: \0"

; A table with all the available commands. The format is a null-terminated
; string with the command itself, followed by 2 bytes with the address to jump
; to to run it.
commands:
hello_command:
        byt "hello\0"
        adr hello
bye_command:
        byt "bye\0"
        adr bye
COMMANDS_SIZE = * - commands


start:
        lda #RAMCR_RAM_ENABLE   ; Disable onboard RAM
        sta RAMCR

        lds #$7eff              ; Set stack to top-of-RAM

        jsr keyboard_init
        jsr serial_init
        jsr graphics_init

        cli                     ; Enable interrupts

line_start:
        ldx #terminal_string
        jsr print
        clr buffer
        ldx #buffer

keyboard_in:
        jsr keyboard_getchar
        cmp a,#0
        beq keyboard_in

        cmp a,#127
        bgt keyboard_in         ; Ignore control characters

        cmp a,#KEY_DELETE
        beq keyboard_in         ; Ignore delete for now, not supported

        cmp a,#KEY_ENTER
        bne .not_exec

        jsr graphics_putchar    ; Put enter on the screen
        jsr exec
        bra line_start
.not_exec:
        ; Make sure not to overflow the input buffer
        cpx #(buffer + BUFFER_SIZE - 2)
        bgt line_start

        sta 0,x                 ; Store character in buffer
        jsr graphics_putchar    ; Put it on the screen
        inx
        clr 0,x                 ; Put a 0 at the new buffer end
        bra keyboard_in

; Figure out what command we intend to run and run it.
exec:
        ldx #buffer
        stx buffer_ptr
        ldx #commands
        stx command_ptr
.loop:
        ldx buffer_ptr
        lda 0,x
        ldx command_ptr
        ldb 0,x

        cba                     ; compare buffer and command bytes
        bne .next_command       ; mismatch, try the next command

        cmp a,#0
        beq .found_command      ; We reached the end of the buffer
                                ; string. Match!
        inx
        stx command_ptr
        ldx buffer_ptr
        inx
        stx buffer_ptr
        bra .loop

.next_command:
        cmp b,#0
        bne .next_loop
        ; We found a 0 in the commands list, we're at the end of a command
        ; string. Next are 2 bytes of addresses to skip, and we need to reset
        ; the buffer pointer.
        inx
        inx
        inx
        stx command_ptr

        cpx #(commands + COMMANDS_SIZE)
        bge .error

        ldx #buffer
        stx buffer_ptr
        bra .loop

.next_loop:
        inx
        ldb 0,x
        bra .next_command

.found_command:
        ; X points to the 0 at the end of the command string, increase by one to
        ; get to the address where the address to jump to is contained. For an
        ; indexed jump, X needs to contain the address to jmp to, so we first do
        ; an indexed load to get to the actual address, then we jump.
        ldx 1,x
        jmp 0,x                 ; rts is in the command we jump to

.error:
        ldx #error_string
        bsr print
        ldx #buffer
        bsr print
        rts

; Print the 0-terminated string pointed to by X. Clobbers A, X.
print:
        lda 0,x
        beq .end
        inx
        jsr graphics_putchar
        bra print
.end:
        rts


hello_string:
        byt "Hello World!\0"
hello:
        ldx #hello_string
        bsr print
        rts

bye_string:
        byt "Bye\0"
bye:
        ldx #bye_string
        bsr print
        rts

irq:
        jsr keyboard_irq
        rti

        org $fff0
vectors:
        adr start               ; SCI
        adr start               ; Timer overflow
        adr start               ; Timer output compare
        adr start               ; Timer input capture
        adr irq                 ; IRQ1
        adr start               ; Software interrupt SWI
        adr start               ; NMI
        adr start               ; Reset / illegal address or instruction
