        cpu 6301

        org $8000

; Program registry structure:
; - autorun address (run this on reset, 0 starts the monitor instead)
; - for each program:
;   - 2 bytes pointing at the next entry
;   - zero-terminated string with program name
;   - 2 bytes address for its entry point
; - after that a last entry with just a $0000 where the next entry address
;   would go

; Set to 0 to start in the monitor, otherwise runs this program at startup.
program_registry_autorun:
        adr $0000
program_registry:
        adr +
        byt "edi\0"
        adr edi_start
+
        adr +
        byt "graphics_test\0"
        adr graphics_test_start
+
        adr +
        byt "hello\0"
        adr hello_start
+
        adr +
        byt "keyboard_test\0"
        adr keyboard_test_start
+
        adr +
        byt "midi_decode\0"
        adr midi_decode_start
+
        adr +
        byt "midi_synth\0"
        adr midi_synth_start
+
        adr +
        byt "opl3_test\0"
        adr opl3_test_start
+
        adr +
        byt "random\0"
        adr random_start
+
        adr +
        byt "sci_echo\0"
        adr sci_echo_start
+
        adr +
        byt "seq\0"
        adr seq_start
+
        adr +
        byt "serial_opl3\0"
        adr serial_opl3_start
+
        adr +
        byt "snake\0"
        adr snake_start
+
        adr +
        byt "test\0"
        adr test_program_start
+
        adr +
        byt "volume_test\0"
        adr volume_test_start
+
        adr $0000               ; End marker

        include programs/edi.s
        include programs/graphics_test.s
        include programs/hello.s
        include programs/keyboard_test.s
        include programs/midi_decode.s
        include programs/midi_synth.s
        include programs/opl3_test.s
        include programs/random.s
        include programs/sci_echo.s
        include programs/seq.s
        include programs/serial_opl3.s
        include programs/snake.s
        include programs/test.s
        include programs/volume_test.s
