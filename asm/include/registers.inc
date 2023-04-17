        ifndef __registers_inc
__registers_inc = 1

DDR1  = $0000                   ; Port 1 Data Direction Register
DDR2  = $0001                   ; Port 2 Data Direction Register
PORT1 = $0002                   ; Port 1 address
PORT2 = $0003                   ; Port 2 address

TRMCR = $0010                   ; Transfer Rate / Mode Control Register
TRCSR = $0011                   ; Transmit / Receive Control Status Register
RDR   = $0012                   ; Receive Data Register
TDR   = $0013                   ; Transmit Data Register
RAMCR = $0014                   ; RAM Control Register

TRMCR_CC0  = %00000100          ; CC0 bit (low bit of clock source settings)

TRCSR_TE   = %00000010          ; Transmit Enable
TRCSR_RE   = %00001000          ; Read Enable
TRCSR_TDRE = %00100000          ; Transmit Data Register Empty
TRCSR_RDRF = %10000000          ; Receive Data Register Full

RAMCR_RAM_ENABLE = %01000000    ; If set enables onboard RAM, if not disables it.

GRAPHICS_SEND_CHAR     = $7fc0  ; Send one character to the graphic terminal,
                                ; advance cursor.
GRAPHICS_CLEAR         = $7fc1  ; Clear graphic terminal.
GRAPHICS_MOVE_CURSOR   = $7fc2  ; Move cursor by (signed) amount.
GRAPHICS_SEND_CHAR_RAW = $7fc3  ; Send a character, keep the cursor in place.

        endif
