        ifndef __instruments_inc
__instruments_inc = 1

        SECTION instruments
        PUBLIC instruments_piano
        PUBLIC instruments_organ

am_vib_egt_ksr_mult function am,vib,egt,ksr,mult, am<<7 + vib<<6 + egt<<5 + ksr<<4 + mult
; TL (total level) is inverted so higher numbers = louder
ksl_tl function ksl,tl, ksl<<6 + (63 - tl)
ar_dr function ar,dr, ar<<4 + dr
; Sustain is inverted so higher = longer sustain
sl_rr function sl,rr, (15 - sl)<<4 + rr
ws function ws, ws
ch_fb_cnt function ch,fb,cnt, ch<<4 + fb<<1 + cnt

instruments_piano:
; OP1
        byt am_vib_egt_ksr_mult(0,0,1,0,1)
        byt ksl_tl(2, 48)
        byt ar_dr(15, 2)
        byt sl_rr(4, 5)
        byt ws(0)
; OP2
        byt am_vib_egt_ksr_mult(0,0,1,0,1)
        byt ksl_tl(0, 57)
        byt ar_dr(15, 2)
        byt sl_rr(7, 6)
        byt ws(0)
; global
        byt ch_fb_cnt(3, 4, 0)

instruments_organ:
; OP1
        byt am_vib_egt_ksr_mult(0,1,1,0,1)
        byt ksl_tl(0, 44)
        byt ar_dr(9, 7)
        byt sl_rr(15, 4)
        byt ws(1)
; OP2
        byt am_vib_egt_ksr_mult(1,0,1,1,1)
        byt ksl_tl(2, 63)
        byt ar_dr(15, 5)
        byt sl_rr(15, 4)
        byt ws(0)
; global
        byt ch_fb_cnt(3, 0, 0)

        ENDSECTION instruments

        endif
