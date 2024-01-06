        ifndef __midi_uart_inc
__midi_uart_inc = 1

        cpu 6301

        include registers

        SECTION midi_uart
        PUBLIC midi_uart_init
        PUBLIC_SHARED midi_uart_read_byte_blocking

UART_CRYSTAL_FREQ = 24000000
UART_BAUD_A       = 31250
UART_BAUD_B       = 31250
UART_DIV_A        = UART_CRYSTAL_FREQ / (UART_BAUD_A * 16)
UART_DIV_B        = UART_CRYSTAL_FREQ / (UART_BAUD_B * 16)
UART_DIV_L_A      = UART_DIV_A # 256
UART_DIV_M_A      = UART_DIV_A / 256
UART_DIV_L_B      = UART_DIV_B # 256
UART_DIV_M_B      = UART_DIV_B / 256

midi_uart_init:
        ; Make sure interrupt pins aren't floating
        lda #%00001000
        sta UART_MCR_A
        sta UART_MCR_B

        lda #$80                ; Set Divisor Latch Access bit
        sta UART_LCR_A
        sta UART_LCR_B

        ; Set divisors for A
        lda #UART_DIV_M_A
        sta UART_DLM_A
        lda #UART_DIV_L_A
        sta UART_DLL_A

        ; Set divisors for B
        lda #UART_DIV_M_B
        sta UART_DLM_B
        lda #UART_DIV_L_B
        sta UART_DLL_B

        lda #%00000011          ; 8 bits, 1 stop, no parity, clear DLA
        sta UART_LCR_A
        sta UART_LCR_B

        ; Enable FIFO mode (bit 0) and reset RX/TX FIFOs (bits 1,2). Trigger
        ; interrupts (if enabled) on every byte (top two bits).
        lda #%00000111
        sta UART_FCR_A
        sta UART_FCR_B

        rts

midi_uart_read_byte_blocking:
        lda UART_LSR_A
        and a,#$01
        beq midi_uart_read_byte_blocking
        lda UART_RBR_A
        rts


        ENDSECTION

        endif
