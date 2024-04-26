        ifndef __delays_inc
__delays_inc = 1

        cpu 6301

        include macros

        SECTION delays
        PUBLIC_SHARED delay_100ms
        PUBLIC_SHARED delay_200ms
        PUBLIC_SHARED delay_500ms

; 50 cycles + ((5 * BB) + 10) * XX
delay_xx macro xx,bb
        pshx                    ; 5 cycles + 6 cycles in the jsr to get here
        pshb                    ; 4

        ; 20
        pshx                    ; 5
        pulx                    ; 4
        nop                     ; 1
        pshx                    ; 5
        pulx                    ; 4
        nop                     ; 1

        ldx #xx                 ; 3, XX
.xloop:
        ldb #0                  ; 2
        ldb #bb                 ; 2, BB
        nop                     ; 1
.bloop:
        nop                     ; 1
        dec b                   ; 1
        bne .bloop              ; 3
        dex                     ; 1
        nop                     ; 1
        bne .xloop              ; 3

        pul b                   ; 3
        pulx                    ; 4
        rts                     ; 5
        endm

delay_100ms:
        delay_xx 1999,8

delay_200ms:
        delay_xx 3999,8

delay_500ms:
        delay_xx 9999,8

        ENDSECTION

        endif
