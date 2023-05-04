        ifndef __registers_inc
__registers_inc = 1

DDR1  = $00                     ; Port 1 Data Direction Register
DDR2  = $01                     ; Port 2 Data Direction Register
PORT1 = $02                     ; Port 1 address
PORT2 = $03                     ; Port 2 address

TCSR  = $08                     ; Timer Control and Status Register
TCH   = $09                     ; Timer counter high
TCL   = $0A                     ; Timer counter low

TCSR_ETOI  = %00000100          ; Enable Timer Overflow Interrupt
TCSR_TOF   = %00100000          ; Timer counter Overflow Flag

TRMCR = $10                     ; Transfer Rate / Mode Control Register
TRCSR = $11                     ; Transmit / Receive Control Status Register
RDR   = $12                     ; Receive Data Register
TDR   = $13                     ; Transmit Data Register
RAMCR = $14                     ; RAM Control Register

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
GRAPHICS_SET_COLUMN    = $7fc4  ; Set the cursor row.
GRAPHICS_SET_ROW       = $7fc5  ; Set the cursor column.  Set the cursor
; position high / low byte. The high byte is not applied until a low byte write
; happens.
GRAPHICS_SET_CURSOR_HIGH = $7fc6
GRAPHICS_SET_CURSOR_LOW  = $7fc7
GRAPHICS_HIDE_CURSOR   = $7fc8  ; 0 = cursor visible, 1 = hidden.


        endif
