        cpu 6301

        org $f000

        include include/macros
        include include/registers
        include include/stdio

INPUT_BUFFER_SIZE = GRAPHICS_TERMINAL_WIDTH
USER_STACK_START = $7dff
MONITOR_STACK_START = $7eff

; Memory map:
; 0000-001f  reserved, internal registers
; 0020-7eff  RAM, of which
;   20-00ff  Zero page addresses, program use
;  100-7dff  Program memory, with stack at 7dff growing down
; 7e00-7eff  Monitor memory, with stack at 7eff growing down
; 7f00-7fff  I/O space, of which:
;   00-  bf  [unused]
;   c0-  ff  Graphics registers
; 8000-ffff  ROM

        orgsave
        org $7e00

input_buffer: ; Input buffer, holds one line
        ds.b INPUT_BUFFER_SIZE
input_buffer_ptr:
        ds.b 2
command_ptr:
        ds.b 2
user_stack_ptr:
        ds.b 2

        if * > $7eff - 64       ; Probably way more than needed
          error "Not enough space for monitor stack left"
        endif

        orgrestore

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
reset_command:
        byt "reset\0"
        adr start
run_command:
        byt "run\0"
        adr run
        byt "r\0"
        adr run
continue_command:
        byt "continue\0"
        adr continue
        byt "c\0"
        adr continue

COMMANDS_SIZE = * - commands


start:
        lda #RAMCR_RAM_ENABLE   ; Disable onboard RAM
        sta RAMCR

        lds #MONITOR_STACK_START ; Set stack to monitor stack location

        jsr stdio_init          ; Initializes serial, keyboard & graphics

        cli                     ; Enable interrupts

line_start:
        ldx #terminal_string
        jsr putstring
        ; Put a string-terminating 0 at the start of the input buffer
        clr input_buffer
        ldx #input_buffer

keyboard_in:
        jsr getchar
        cmp a,#0
        beq keyboard_in

        cmp a,#127
        bgt keyboard_in         ; Ignore control characters

        cmp a,#KEY_DELETE
        beq keyboard_in         ; Ignore delete for now, not supported

        cmp a,#KEY_ENTER
        bne .not_enter

        jsr putchar             ; Put enter on the screen
        jsr exec
        bra line_start

.not_enter:
        ; Make sure not to overflow the input buffer
        cpx #(input_buffer + INPUT_BUFFER_SIZE - 2)
        bgt line_start

        sta 0,x                 ; Store character in buffer
        jsr putchar             ; Put it on the screen
        inx
        clr 0,x                 ; Put a 0 at the new buffer end
        bra keyboard_in

; Figure out what command we intend to run and run it.
exec:
        ldx #input_buffer
        stx input_buffer_ptr
        ldx #commands
        stx command_ptr
.loop:
        ldx input_buffer_ptr
        lda 0,x
        ldx command_ptr
        ldb 0,x

        cba                     ; compare buffer and command bytes
        bne .next_command       ; mismatch, try the next command

        tst a                   ; 0 or negative (but we have no negatives in A)
        beq .found_command      ; We reached the end of the buffer
                                ; string. Match!
        inx
        stx command_ptr
        ldx input_buffer_ptr
        inx
        stx input_buffer_ptr
        bra .loop

.next_command:
        tst b                   ; 0 or negative but no negatives in B
        bne .next_loop
        ; We found a 0 in the commands list, we're at the end of a command
        ; string. Next are 2 bytes of addresses to skip, and we need to reset
        ; the input buffer pointer.
        inx
        inx
        inx
        stx command_ptr

        cpx #(commands + COMMANDS_SIZE)
        bge .error

        ldx #input_buffer
        stx input_buffer_ptr
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
        jsr putstring
        ldx #input_buffer
        jsr putstring
        rts

hello_string:
        byt "Hello World!\0"
hello:
        ldx #hello_string
        jsr putstring
        rts

bye_string:
        byt "Bye\0"
bye:
        ldx #bye_string
        jsr putstring
        rts

run:
        lds #USER_STACK_START
        ; Push the monitor start address on the user stack so a program can rts
        ; back to it.
        ldx #line_start
        pshx
        jmp test_user_program

continue:
        lds user_stack_ptr

        ; The user program was interrupted and still has program counter,
        ; accumulators, etc on the user stack. RTI restores these and continues
        ; execution.
        rti

irq:
        ; The 6301 puts stack pointer, accumulators, register flags etc on the
        ; stack before entering an interrupt routine. We don't need to take
        ; precautions to preserve them.
        jsr keyboard_irq
        bvs .invoke_monitor     ; keyboard IRQ sets V if 'break' key is pressed
        rti

.invoke_monitor:
        sts user_stack_ptr      ; Store the current user stack pointer
        lds #MONITOR_STACK_START
        cli                     ; Re-enable interrupts, they are disabled when
                                ; an interrupt is called
        jmp line_start


; A small test program that just echos the typed characters back on the screen
; or exits with 'X' (capital X). Prints a string when freshly started to help
; distinguish 'run' from 'continue'.
test_string:
        byt "\nUser program start\n\0"
test_user_program:
        clr GRAPHICS_CLEAR      ; Clear screen
        ldx #test_string
        jsr putstring
        jsr test_user_program_function
        rts                     ; Back to the monitor

test_user_program_function:
        jsr getchar
        tst a
        beq test_user_program_function
        jsr putchar

        cmp a,#"X"
        bne test_user_program_function
        rts

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
