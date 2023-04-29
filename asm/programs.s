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
        byt "test\0"
        adr test_program_start
+
        adr $0000               ; End marker

        include programs/hello.s
        include programs/test.s
