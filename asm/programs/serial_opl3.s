        cpu 6301

        include ../stdlib.inc

        SECTION serial_opl3
        PUBLIC serial_opl3_start

serial_opl3_start:

.read_loop:
        jsr serial_read_byte_blocking

        cmp a,#'w'              ; got register array 0 command
        bne +

        jsr serial_read_byte_blocking
        sta OPL3_ADDRESS_ARRAY0
        jsr serial_read_byte_blocking
        sta OPL3_DATA_WRITE
        bra .read_loop
+
        cmp a,#'W'              ; register array 1 command
        bne +
        jsr serial_read_byte_blocking
        sta OPL3_ADDRESS_ARRAY1
        jsr serial_read_byte_blocking
        sta OPL3_DATA_WRITE
        bra .read_loop

+
        bra .read_loop

        ENDSECTION
