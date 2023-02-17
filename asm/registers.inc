        ifndef __registers_inc
__registers_inc = 1
        
DDR1  = $0000                   ; Data Direction Register port 1
PORT1 = $0002                   ; Port 1 address

TRMCR = $0010                   ; Transfer Rate / Mode Control Register
TRCSR = $0011                   ; Transmit / Receive Control Status Register
RDR   = $0012                   ; Receive Data Register
TDR   = $0013                   ; Transmit Data Register

TRMCR_CC0  = %00000100          ; CC0 bit (low bit of clock source settings)

TRCSR_TE   = %00000010          ; Transmit Enable
TRCSR_RE   = %00001000          ; Read Enable
TRCSR_TDRE = %00100000          ; Transmit Data Register Empty
TRCSR_RDRF = %10000000          ; Receive Data Register Full

        endif