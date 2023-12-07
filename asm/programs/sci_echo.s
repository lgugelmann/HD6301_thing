; Echo whatever comes in from the serial console

        cpu 6301

        include ../stdlib.inc

        SECTION sci_echo
        PUBLIC sci_echo_start

sci_echo_start:
        jsr serial_read_byte_blocking
        jsr serial_send_byte
        bra sci_echo_start
        rts

        ENDSECTION
