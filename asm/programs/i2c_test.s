        cpu 6301

        include ../stdlib.inc

        SECTION i2c_test_program
        PUBLIC i2c_test_start

I2C_ADDRESS = %00111111

i2c_test_start:
        jsr getchar

        ; Quit
        cmp a,#"q"
        bne +
        rts
+
        cmp a,#"0"
        bne +
        ldb #$00
        jsr i2c_test_send_b
        bra i2c_test_start
+
        cmp a,#"1"
        bne +
        ldb #$01
        jsr i2c_test_send_b
        bra i2c_test_start
+
        cmp a,#"2"
        bne +
        ldb #$02
        jsr i2c_test_send_b
        bra i2c_test_start
+
        cmp a,#"u"
        bne +
        inc IO_DDRA
+
        cmp a,#"d"
        bne +
        dec IO_DDRA
+
        bra i2c_test_start

; Send the byte in B
i2c_test_send_b:
        lda #I2C_ADDRESS
        jsr i2c_send_byte
        tst a
        beq .success
        lda #"F"
        jsr putchar
        bra i2c_test_start
.success:
        lda #"W"
        jsr putchar
        rts


        ENDSECTION
