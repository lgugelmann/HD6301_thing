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
; computed with Fm = 14.31818MHz and A4=440Hz, equal temperament, and centered
; to cover almost all of the piano key range. The first valid midi note number
; is 21 (A0), the last 104 (G#7).

sound_midi_to_fnuml:
        dc.b [21]$00        ; padding bytes to avoid having to sub 21
        ;      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23
        .byt $22,$33,$46,$59,$6D,$83,$9A,$B3,$CC,$E8,$05,$23

sound_midi_to_kbfh:
        dc.b [21]$00        ; padding bytes to avoid having to sub 21
        ;      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11
        .byt $25,$25,$25,$25,$25,$25,$25,$25,$25,$25,$26,$26
        .byt $29,$29,$29,$29,$29,$29,$29,$29,$29,$29,$2A,$2A
        .byt $2D,$2D,$2D,$2D,$2D,$2D,$2D,$2D,$2D,$2D,$2E,$2E
        .byt $31,$31,$31,$31,$31,$31,$31,$31,$31,$31,$32,$32
        .byt $35,$35,$35,$35,$35,$35,$35,$35,$35,$35,$36,$36
        .byt $39,$39,$39,$39,$39,$39,$39,$39,$39,$39,$3A,$3A
        .byt $3D,$3D,$3D,$3D,$3D,$3D,$3D,$3D,$3D,$3D,$3E,$3E

        endif
