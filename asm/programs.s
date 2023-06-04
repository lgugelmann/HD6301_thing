        cpu 6301

        org $8000

; Program registry structure:
; - for each program:
;   - 2 bytes pointing at the next entry
;   - zero-terminated string with program name
;   - 2 bytes address for its entry point
; - after that a last entry with just a $0000 where the next entry address
;   would go

program_registry:
        adr +
        byt "hello\0"
        adr hello_start
+
        adr +
        byt "sci_echo\0"
        adr sci_echo_start
+
        adr +
        byt "test\0"
        adr test_program_start
+
        adr +
        byt "snake\0"
        adr snake_start
+
        adr +
        byt "random\0"
        adr random_start
+
        adr +
        byt "serial_opl3\0"
        adr serial_opl3_start
+
        adr $0000               ; End marker

        include programs/hello.s
        include programs/random.s
        include programs/sci_echo.s
        include programs/serial_opl3.s
        include programs/snake.s
        include programs/test.s
