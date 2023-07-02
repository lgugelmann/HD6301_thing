        ifndef __sound_frequency_tables_inc
__sound_frequency_tables_inc = 1

; From the YMF262 datasheet:
;   F = Fnum * Fs * 2^(Block-1) / 2^19
; Where:
; - F is the frequency played
; - Fnum, Block are register settings
; - Fs = Fm/288
; - Fm = the input clock frequency
;
; The tables below map MIDI numbers to FNUM(L) and KON/BLOCK/FNUM(H). They are
; computed with Fm = 13MHz in mind and centered to cover the piano key
; range. The first valid midi note is 21, the last 105.

sound_midi_to_fnuml:
        dc.b [21]$00        ; padding bytes to avoid having to sub 21
        ;      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b
        .byt $3f,$52,$67,$7c,$92,$aa,$c4,$df,$fb,$19,$39,$5b

sound_midi_to_kbfh:
        dc.b [21]$00        ; padding bytes to avoid having to sub 21
        ;      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11
        .byt $25,$25,$25,$25,$25,$25,$25,$25,$25,$26,$26,$26
        .byt $29,$29,$29,$29,$29,$29,$29,$29,$29,$2a,$2a,$2a
        .byt $2d,$2d,$2d,$2d,$2d,$2d,$2d,$2d,$2d,$2e,$2e,$2e
        .byt $31,$31,$31,$31,$31,$31,$31,$31,$31,$32,$32,$32
        .byt $35,$35,$35,$35,$35,$35,$35,$35,$35,$36,$36,$36
        .byt $39,$39,$39,$39,$39,$39,$39,$39,$39,$3a,$3a,$3a
        .byt $3d,$3d,$3d,$3d,$3d,$3d,$3d,$3d,$3d,$3e,$3e,$3e

        endif
