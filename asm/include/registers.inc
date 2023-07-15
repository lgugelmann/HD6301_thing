        ifndef __registers_inc
__registers_inc = 1

;;
;; HD6301 internal registers
;;
DDR1  = $00                     ; Port 1 Data Direction Register
DDR2  = $01                     ; Port 2 Data Direction Register
PORT1 = $02                     ; Port 1 address
PORT2 = $03                     ; Port 2 address

TCSR  = $08                     ; Timer Control and Status Register
TCH   = $09                     ; Timer counter high
TCL   = $0A                     ; Timer counter low

TRMCR = $10                     ; Transfer Rate / Mode Control Register
TRCSR = $11                     ; Transmit / Receive Control Status Register
RDR   = $12                     ; Receive Data Register
TDR   = $13                     ; Transmit Data Register
RAMCR = $14                     ; RAM Control Register

TRMCR_CC0  = %00000100          ; CC0 bit (low bit of clock source settings)

TCSR_ETOI  = %00000100          ; Enable Timer Overflow Interrupt
TCSR_TOF   = %00100000          ; Timer counter Overflow Flag

TRCSR_TE   = %00000010          ; Transmit Enable
TRCSR_RE   = %00001000          ; Read Enable
TRCSR_TDRE = %00100000          ; Transmit Data Register Empty
TRCSR_RDRF = %10000000          ; Receive Data Register Full

RAMCR_RAM_ENABLE = %01000000    ; If set enables onboard RAM, if not disables it.

;;
;; OPL3 Sound registers
;;
OPL3_ADDRESS_ARRAY0 = $7f80     ; A0 = 0, A1 = 0
OPL3_STATUS_READ    = $7f80     ; Same memory location as address write
OPL3_ADDRESS_ARRAY1 = $7f82     ; A0 = 0, A1 = 1
OPL3_DATA_WRITE     = $7f81     ; A0 = 1, A1 = 0 (A1 could be arbitrary)

;; Constants for register addresses in OPL3 itself, not part of the memory map.
OPL3_TIMER1        = $02
OPL3_TIMER2        = $03
OPL3_TIMER_CONTROL = $04

;;
;; Pico Graphics registers
;;
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
GRAPHICS_HIDE_CURSOR     = $7fc8  ; 0 = cursor visible, 1 = hidden.
; Format is CCRRGGBB where CC=0 is foreground, CC=1 is background, CC=2 is both
GRAPHICS_SET_COLOR       = $7fc9


        endif
