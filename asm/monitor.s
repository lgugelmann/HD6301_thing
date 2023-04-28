        cpu 6301

        org $f000

        include include/macros
        include include/registers
        include include/stdio

INPUT_BUFFER_SIZE = GRAPHICS_TERMINAL_WIDTH - 3
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
no_user_program_error_string:
        byt "Error: no user program running.\0"
extra_parameters_error_string:
        byt "Error: extra unexpected parameters found.\0"
missing_parameters_error_string:
        byt "Error: this command requires some parameters.\0"

; A table with all the available commands. The format is a null-terminated
; string with the command itself, followed by 1 byte with the number of
; parameters, then 2 bytes with the address to jump to to run it.
commands:
reset_command:
        byt "reset\0"
        byt $00
        adr start
run_command:
        byt "run\0"
        byt $01
        adr run
        byt "r\0"
        byt $01
        adr run
continue_command:
        byt "continue\0"
        byt $00
        adr continue
        byt "c\0"
        byt $00
        adr continue
print_command:
        byt "print\0"
        byt $00
        adr print
        byt "p\0"
        byt $00
        adr print

COMMANDS_SIZE = * - commands


start:
        lda #RAMCR_RAM_ENABLE   ; Disable onboard RAM
        sta RAMCR

        lds #MONITOR_STACK_START ; Set stack to monitor stack location

        jsr stdio_init          ; Initializes serial, keyboard & graphics

        ldx #0
        stx user_stack_ptr

        cli                     ; Enable interrupts

line_start:
        ldx #terminal_string
        jsr putstring
        ; Put a string-terminating 0 at the start of the input buffer
        clr input_buffer
        ldx #input_buffer

keyboard_in:
        jsr getchar
        tst a
        beq keyboard_in

        cmp a,#127
        bhi keyboard_in         ; Ignore control characters

        cmp a,#KEY_DELETE
        beq keyboard_in         ; Ignore delete for now, not supported

        cmp a,#KEY_ENTER
        bne .not_enter

        jsr putchar             ; Put enter on the screen
        jsr exec
        bra line_start

.not_enter:
        ; Make sure not to overflow the input buffer
        cpx #(input_buffer + INPUT_BUFFER_SIZE - 1)
        bhi line_start

        sta 0,x                 ; Store character in buffer
        jsr putchar             ; Put it on the screen
        inx
        clr 0,x                 ; Put a 0 at the new buffer end
        bra keyboard_in

; Figure out what command we intend to run and run it. We run through the
; command list one by one and compare it to the input buffer and either find a
; match or reach the end of the command list.
exec:
        ldx #input_buffer
        stx input_buffer_ptr
        ldx #commands
        stx command_ptr
.command_match_loop:
        ; The code after this assumes X is command_ptr. Don't reorder.
        ldx input_buffer_ptr
        lda 0,x
        ldx command_ptr
        ldb 0,x

        ; If B is 0 we found a command boundary.
        beq .command_boundary

        cba                     ; compare buffer and command bytes
        bne .next_command       ; mismatch, try the next command

        ; Match, move on to the next character.
        inx
        stx command_ptr
        ldx input_buffer_ptr
        inx
        stx input_buffer_ptr
        bra .command_match_loop

.command_boundary:
        ; A needs to be 0 (no-parameter command) or a space (1+ parameters).
        tab
        ora #" "
        cmp a,#" "              ; This is true for A=0 and A=' '
        bne .next_command       ; No match, try the next command
        tba

        ldb 1,x                 ; Get the parameter count
        bne .parameters_expected

        tst a                   ; No parameters, we need a 0 in A
        beq .found_command

        ldx #extra_parameters_error_string
        jsr putstring
        rts

.parameters_expected:
        cmp a,#" "
        beq .found_command      ; Expecting parameters, got a space, up to the
                                ; command implementation to parse them.

        ldx #missing_parameters_error_string
        jsr putstring
        rts

.next_command:
        ; We need to continue looping through the command buffer until we find
        ; the end of the current one (if we're not there yet).
        tst b
        beq .command_end

        inx
        ldb 0,x
        bra .next_command

.command_end:
        ; We found a 0 in the commands list, we're at the end of a command
        ; string. Skip that plus 3 bytes of parameters and addresses to get to
        ; the next one or the end of the list.
        ldb #(1 + 3)
        abx
        stx command_ptr

        cpx #(commands + COMMANDS_SIZE)
        bge .error

        ; Reset the input buffer pointer to the start and try matching the next
        ; command.
        ldx #input_buffer
        stx input_buffer_ptr
        bra .command_match_loop

.found_command:
        ; X points to the 0 at the end of the command string, increase by two to
        ; get to the address where the address to jump to is contained. For an
        ; indexed jump, X needs to contain the address to jmp to, so we first do
        ; an indexed load to get to the actual address, then we jump.
        ldx 2,x
        jmp 0,x
        ; rts for 'exec' is in the command we jump to

.error:
        ldx #error_string
        jsr putstring
        ldx #input_buffer
        jsr putstring
        rts

run:
        lds #USER_STACK_START
        ; jsr and not jmp so the user program can rts back to the monitor.
        jsr test_user_program

        ; Clear the user stack pointer to flag that there isn't a user program
        ; runnning.
        ldx #0
        stx user_stack_ptr
        ; Can't rts here: the monitor stack might have been changed around by
        ; invoking the monitor program from the user program.
        lds #MONITOR_STACK_START
        jmp line_start

continue:
        ldx user_stack_ptr
        cpx #0
        beq .error

        lds user_stack_ptr
        ; The user program was interrupted and still has program counter,
        ; accumulators, etc on the user stack. RTI restores these and continues
        ; execution.
        rti
.error:
        ldx #no_user_program_error_string
        jsr putstring
        rts

; Print the current status registers, accumulators, program counter, and stack
; pointer of the (paused) user program. This assumes the data is on the user
; stack in the following order:
; SP+1: condition code register
; SP+2: B register
; SP+3: A register
; SP+4: X register high
; SP+5: X register low
; SP+6: program counter high
; SP+7: program counter low
print:
        ldx user_stack_ptr
        cpx #0
        beq .error

        ldx #.print_string
        jsr putstring
        ldx user_stack_ptr

        lda 1,x                 ; CC
        jsr putchar_bin
        lda #" "
        jsr putchar

        lda 3,x                 ; A
        jsr putchar_hex
        lda #" "
        jsr putchar

        lda 2,x                 ; B
        jsr putchar_hex
        lda #" "
        jsr putchar

        lda 4,x                 ; X
        jsr putchar_hex
        lda 5,x
        jsr putchar_hex
        lda #" "
        jsr putchar

        lda 6,x                 ; PC
        jsr putchar_hex
        lda 7,x
        jsr putchar_hex
        lda #" "
        jsr putchar

        xgdx                    ; Exchange X with D (A|B)
        jsr putchar_hex
        tba
        jsr putchar_hex

        rts

.print_string:
        byt "--HINZVC  A  B    X   PC   SP\n\0"

.error:
        ldx #no_user_program_error_string
        jsr putstring
        rts

irq:
        ; The 6301 puts stack pointer, accumulators, register flags etc on the
        ; stack before entering an interrupt routine. We don't need to take
        ; precautions to preserve them.
        jsr keyboard_irq
        bvs invoke_monitor      ; keyboard IRQ sets V if 'break' key is pressed
        rti

invoke_monitor:
        ; Check whether we're already in the monitor program by looking at where
        ; the stack pointer currently is. If it's above the top of user memory
        ; (topmost place the user stack should be) we're already in the monitor.
        tsx
        cpx #USER_STACK_START
        bls .invoke
        rti

.invoke:
        ; Store the user stack pointer, reset and switch to the monitor stack
        sts user_stack_ptr
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
        bne +
        rts

+
        cmp a,#"S"
        bne +
        swi

+
        bra test_user_program_function

        org $fff0
vectors:
        adr start               ; SCI
        adr start               ; Timer overflow
        adr start               ; Timer output compare
        adr start               ; Timer input capture
        adr irq                 ; IRQ1
        adr invoke_monitor      ; Software interrupt SWI
        adr start               ; NMI
        adr start               ; Reset / illegal address or instruction
