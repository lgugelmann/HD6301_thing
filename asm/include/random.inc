        ifndef __random_inc
__random_inc = 1

        cpu 6301

        include macros
        include registers

        SECTION random
        PUBLIC random_init
        PUBLIC_SHARED random_byte
        PUBLIC_SHARED random_srand

        zp_var random_seed, 1

random_init:
        lda #$01
        sta random_seed
        rts

; Seed the random generator
random_srand:
        tst a
        beq .end               ; 0 as seed just creates a string of 0s, avoid it
        sta random_seed
.end
        rts

; Returns a pseudo-random byte in A. The code below is basically a loop through
; all 256 possible values.
random_byte:
        lda random_seed
        ; This forces eor when the seed ends up 0 to avoid locking at 0
        beq do_eor
        asl a
        ; If the input was $80, skip the eor. This avoids both 0 and $80 from
        ; giving the result of #$1d.
        beq no_eor
        ; Otherwise add #$1d if the top bit was 0
        bcc no_eor
do_eor:
        eor a,#$1d
no_eor:
        sta random_seed
        rts

        ENDSECTION random

        endif
