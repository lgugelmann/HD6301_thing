        ifndef __memory_map_inc
__memory_map_inc = 1

; Memory map:
; 0000-001f  reserved, internal registers
; 0020-7eff  RAM, of which
;   20-00ff  Zero page addresses, program use
;  100-79ff  Program memory, with stack at 79ff growing down
; 7a00-7eff  Monitor memory, with stack at 7eff growing down
; 7f00-7fff  I/O space, of which:
;   00-  1f  [unused]
;   20 - 3f  65C22 I/O            0111 1111 001x xxxx
;   40-  7f  MIDI / UART          0111 1111 01xx xxxx
;   80-  bf  OPL3 sound           0111 1111 10xx xxxx
;   c0-  ff  Graphics registers   0111 1111 11xx xxxx
; 8000-ffff  ROM

; Hardware-defined boundaries
CPU_HW_REGISTERS_START = $0000
CPU_HW_REGISTERS_END   = $001f
RAM_START              = $0020
ZP_START               = RAM_START
ZP_END                 = $00ff
RAM_END                = $7eff
IO_SPACE_START         = $7f00
IO_SPACE_END           = $7fff
ROM_START              = $8000
ROM_END                = $ffff

MIN_USER_STACK_SIZE = $100
SYSTEM_MEMORY_START = $7a00
USER_STACK_START    = SYSTEM_MEMORY_START - 1
MONITOR_STACK_START = RAM_END

        endif
