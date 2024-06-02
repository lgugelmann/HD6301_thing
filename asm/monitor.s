        cpu 6301

        org $e000

        include include/delays
        include include/file
        include include/io
        include include/macros
        include include/map
        include include/memory_map
        include include/midi_uart
        include include/registers
        include include/random
        include include/sd_card
        include include/sound
        include include/stdio
        include include/timer

        SECTION monitor

INPUT_BUFFER_SIZE = GRAPHICS_TERMINAL_WIDTH - 3

        ; Input buffer, holds one line
        reserve_system_memory input_buffer,INPUT_BUFFER_SIZE
        reserve_system_memory input_buffer_param0_ptr,2
        reserve_system_memory command_ptr,2
        reserve_system_memory user_stack_ptr,2
        reserve_system_memory monitor_stack_ptr,2
        ; Buffer to hold data for file I/O
        reserve_system_memory file_io_buffer,512
        ; Points 1 past the last valid byte of the buffer above
        reserve_system_memory file_io_end_ptr,2

terminal_string:
        byt KEY_ENTER
        byt "> \0"
unknown_command_error_string:
        byt "Unknown command: '\0"
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
        adr +
        byt "reset\0"
        byt $00
        adr start
+
        adr +
        byt "run\0"
        byt $01
        adr run
+
        adr +
        byt "r\0"
        byt $01
        adr run
+
        adr +
        byt "continue\0"
        byt $00
        adr continue
+
        adr +
        byt "c\0"
        byt $00
        adr continue
+
        adr +
        byt "print\0"
        byt $00
        adr print
+
        adr +
        byt "p\0"
        byt $00
        adr print
+
        adr +
        byt "list\0"
        byt $00
        adr list_command
+
        adr +
        byt "clear\0"
        byt $00
        adr clear_command
+
        adr +
        byt "cd\0"
        byt $01
        adr cd_command
+
        adr +
        byt "ls\0"
        byt $00
        adr ls_command
+
        adr +
        byt "cat\0"
        byt $01
        adr cat_command
+
        adr $0000

start:
        lda #RAMCR_RAM_ENABLE   ; Disable onboard RAM
        sta RAMCR

        lds #MONITOR_STACK_START ; Set stack to monitor stack location
        sts monitor_stack_ptr

        jsr io_init
        jsr midi_uart_init
        jsr random_init
        jsr sound_init
        jsr stdio_init          ; Initializes serial, keyboard & graphics
        jsr timer_init
        jsr sd_card_init
        jsr file_init

        ldx #0
        stx user_stack_ptr

        cli                     ; Enable interrupts

        ldx $8000               ; Autorun program address
        beq line_start          ; 0 in the autorun address, start in the monitor
        jsr run_program_in_x

line_start:
        ldx #terminal_string
        jsr putstring
        ; Put a string-terminating 0 at the start of the input buffer
        clr input_buffer
        ldx #input_buffer       ; Track current position in input buffer

.keyboard_in:
        jsr getchar
        beq .keyboard_in

        cmp a,#127
        bhi .keyboard_in        ; Ignore control characters

        cmp a,#KEY_BACKSPACE
        beq .backspace
        cmp a,#KEY_DELETE
        bne .not_backspace

.backspace:
        ; Nothing to do for empty input buffer.
        cpx #input_buffer
        beq .keyboard_in
        ; Overwrite last character in buffer with 0.
        dex
        clr 0,x
        ; Expect output routines to draw BACKSPACE correctly.
        lda #KEY_BACKSPACE
        jsr putchar
        bra .keyboard_in

.not_backspace:
        cmp a,#KEY_ENTER
        bne .printable_char

        jsr putchar             ; Put enter on the screen
        jsr exec
        bra line_start

.printable_char:
        ; Make sure not to overflow the input buffer
        cpx #(input_buffer + INPUT_BUFFER_SIZE - 1)
        bhi line_start

        sta 0,x                 ; Store character in buffer
        jsr putchar             ; Put it on the screen
        inx
        clr 0,x                 ; Put a 0 at the new buffer end
        bra .keyboard_in

; Figure out what command we intend to run and run it.
exec:
        ldx #input_buffer
        pshx
        ldx #commands
        jsr map_find
        bcc .unknown_command_error ; match / no-match is indicated with C

        stx command_ptr         ; Store a pointer to the matching command

        ldb 0,x                 ; Get the expected parameter count
        bne .parameters_expected

        ; Get the pointer for the input buffer position put on the stack by the
        ; map_find above.
        pulx

        lda 0,x                 ; No parameters, we need a 0 in that position
        beq .found_command

        ldx #extra_parameters_error_string
        jsr putstring
        rts

.parameters_expected:
        ; Get the pointer for the input buffer position put on the stack by the
        ; map_find above.
        pulx
        lda 0,x
        cmp a,#" "
        beq .found_params_command ; Expecting a space, got one

        ldx #missing_parameters_error_string
        jsr putstring
        rts

.found_params_command:
        inx
        stx input_buffer_param0_ptr

.found_command:
        ldx command_ptr

        ; X points to the first byte past the command string, the address to
        ; jump to is 1 byte beyond that. For an indexed jump, X needs to contain
        ; the address to jmp to, so we first do an indexed load to get to the
        ; actual address, then we jump.
        ldx 1,x
        jmp 0,x
        ; rts for 'exec' is in the command we jump to

.unknown_command_error:
        ldx #unknown_command_error_string
        jsr putstring
        ldx #input_buffer
        jsr putstring
        lda #"'"
        jsr putchar
        rts

; Run the program named in the first parameter. The valid program names are
; listed in the program registry at $8002+.
run:
        ldx input_buffer_param0_ptr
        pshx
        ldx #$8002
        jsr map_find
        bcc .unknown_program_error

        ldx 0,x                 ; X is at the location of the start address
        ins                     ; Discard the 2 byte return value on the stack
        ins
        bra run_program_in_x

.unknown_program_error_string:
        byt "Error: unknown program '\0"

.unknown_program_error:
        ldx #.unknown_program_error_string
        jsr putstring
        ldx input_buffer_param0_ptr
        jsr putstring
        lda #"'"
        jsr putchar
        rts

run_program_in_x:
        sts monitor_stack_ptr
        lds #USER_STACK_START
        ; jsr and not jmp so the user program can rts back to the monitor.
        jsr 0,x

        lds monitor_stack_ptr

        ; Clear the user stack pointer to flag that there isn't a user program
        ; runnning.
        ldx #0
        stx user_stack_ptr

        rts

; Resume the currently running user program by 'rti' from the user stack.
continue:
        ldx user_stack_ptr
        cpx #0
        beq .error

        sts monitor_stack_ptr
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
        byt "\n--HINZVC  A  B    X   PC   SP\n\0"

.error:
        ldx #no_user_program_error_string
        jsr putstring
        rts

; List all program names and their entry points in the program registry. The
; code is a bit more convoluted because it does not assume that all entries of
; the registry are contiguous. In theory they could be all over the place.
list_command:

        ; Program registry is at the start of ROM.
        ldx #$8002
.loop:
        ldd 0,x                 ; Load next entry pointer in (A|B)
        beq .end                ; If it's 0 we're done

        ; Store the pointer on the stack for later.
        xgdx
        pshx
        xgdx

        ; Skip the two pointer bytes
        inx
        inx

        ; Prints the program name and advances X to the 0 terminator.
        jsr putstring

        ; Print program address
        lda #" "
        jsr putchar
        inx
        lda 0,x                 ; Program address high
        jsr putchar_hex
        inx
        lda 0,x                 ; Program address low
        jsr putchar_hex
        lda #"\n"
        jsr putchar

        pulx                    ; Set X to the next entry
        bra .loop

.end:
        rts

; Clear the screen, reset the cursor to the top.
clear_command:
        clr GRAPHICS_CLEAR
        jsr serial_clear_screen
        clr GRAPHICS_HIDE_CURSOR
        rts

; List the contents of the current working directory.
ls_command:
        jmp file_ls

; Change the current working directory to the one in the first parameter. Only
; one hop is supported at a time.
cd_command:
        ldx input_buffer_param0_ptr
        jmp file_cd

; Print the contents of the file named in the argument on the terminal. The file
; must be in the current directory.
cat_command:
        ldx input_buffer_param0_ptr
        ldd #file_io_buffer
        jsr file_open
        tst a
        bmi .file_open_error    ; V is set -> A > 127, i.e. error

.read_loop:
        psh a                   ; Store file descriptor
        jsr file_read
        tst a
        bmi .file_read_error    ; V is set -> A > 127, i.e. error

        pshx                    ; Put X (bytes read) on the stack
        ldx #file_io_buffer
        pshx                    ; Put file buffer pointer on the stack

.high_loop:
        tsx
        ldd 2,x                 ; Bytes counter
        beq .done_printing
        tst b
        ; If B is 0 and D isn't we can subtract 1 from the high byte and loop
        ; 256 times.
        beq .sub256
        ; If B is nonzero we subtract the number we're about to loop over.
        clr 3,x
        bra .sub_done
.sub256:
        dec 2,x                 ; Decrement the high byte
.sub_done:
        pulx                    ; File buffer pointer
.low_loop:
        lda 0,x
        jsr putchar
        inx
        dec b
        bne .low_loop
        pshx                    ; File buffer pointer
        bra .high_loop

.done_printing:
        drop2                   ; File buffer pointer
        drop2                   ; Bytes read
        pul a                   ; Get the file descritpor number back
        jsr file_at_eof         ; Sets Z if at EOF
        bne .read_loop

        jmp file_close
        ; rts

.file_open_error:
        jsr putchar_hex
        ldx #.file_open_error_string
        jmp putstring

.file_read_error:
        jsr putchar_hex
        pul a                   ; Get the FD number back
        jsr file_close
        ldx #.file_read_error_string
        jmp putstring

.file_open_error_string:
        byt ": Failed to open file\n\0"

.file_read_error_string:
        byt ": Failed to read file\n\0"

irq:
        ; The 6301 puts stack pointer, accumulators, register flags etc on the
        ; stack before entering an interrupt routine. We don't need to take
        ; precautions to preserve them.

        ; IRQ handlers set C to indicate that they handled an interrupt
        clc
        ; OPL3 timer IRQ go first. If we don't handle them within 80uS there is
        ; a chance of dropping timer events. See sound.inc for details.
        jsr sound_irq
        bcs .handled            ; IRQ handlers set C if they handled their IRQ

        jsr keyboard_irq
        bvs invoke_monitor      ; keyboard IRQ sets V if 'break' key is pressed
        bcs .handled

        jsr io_irq
        bcs .handled

        ; Invoke the monitor on an unexpected unhandled interrupt
        bra invoke_monitor
.handled:
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
        lds monitor_stack_ptr
        cli                     ; Re-enable interrupts, they are disabled when
                                ; an interrupt is called
        jsr print
        jmp line_start


        org $fff0
vectors:
        adr serial_irq          ; SCI
        adr timer_irq           ; Timer overflow
        adr start               ; Timer output compare
        adr start               ; Timer input capture
        adr irq                 ; IRQ1
        adr invoke_monitor      ; Software interrupt SWI
        adr start               ; NMI
        adr start               ; Reset / illegal address or instruction

        ENDSECTION
