        cpu 6301

        include ../stdlib.inc

        SECTION volume_test
        PUBLIC volume_test_start

        zp_var midi_ksl_tl,1
        zp_var midi_velocity,1

; A quick program to test MIDI -> OPL3 volume conversions
volume_test_start:
        clr midi_ksl_tl
        lda #127
        jsr test
        lda #96
        jsr test
        lda #64
        jsr test
        lda #31
        jsr test
        lda #17
        jsr test
        rts

test:
        sta midi_velocity
        jsr putchar_dec
        lda #' '
        jsr putchar
        jsr compute_volume
        jsr putchar_dec
        lda #KEY_ENTER
        jsr putchar
        rts

; Compute and set the correct volume for the 'midi_note' to be played at
; velocity 'midi_velocity' on an FM-modulated instrument with OP2 KSL/TL
; settings at 'midi_ksl_tl'. Volume is returned in A, clobbers B.
compute_volume:
        ; Algorithm:
        ; TL = 63 - ((63-patch TL)*2*(OPL3-scaled MIDI volume) / 128)
        ; 63 - ((instr volume)*(midi volume) >> 6)
        lda midi_ksl_tl
        and a,#$3f              ; Drop the KSL bits
        eor a,#$3f              ; 63-TL to get volume instead of attenuation.
        ; MIDI to OPL3 volume: M 0..31: M, 32..63: 16+M/2, 64..127: 32+M/4
        ; 32+M/4 can be decomposed as: 16 + (32 + M>>1)>>1
        ldb midi_velocity
        cmp b,#32
        blt .low
        cmp b,#64
        blt .mid
        lsr b
        add b,#32
.mid:
        lsr b
        add b,#16
.low:
        mul                     ; Result is in D=A|B
        asld                    ; (A|B >> 6) is equivalent to D << 2 and taking
                                ; the top byte.
        asld
        eor a,#$3f              ; 63-A
        ldb midi_ksl_tl
        and b,#$c0              ; Keep only the KSL bits
        aba                     ; Put computed TL and KSL back together
        rts

        ENDSECTION
