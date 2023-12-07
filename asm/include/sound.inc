        ifndef __sound_inc
__sound_inc = 1

        include macros
        include registers
        include sound_frequency_tables

        SECTION sound
        PUBLIC sound_init
        PUBLIC sound_irq

        PUBLIC_SHARED sound_load_instrument
        PUBLIC_SHARED sound_play_note
        PUBLIC_SHARED sound_set_attenuation
        PUBLIC_SHARED sound_stop_note
        PUBLIC_SHARED sound_timer1_callback
        PUBLIC_SHARED sound_timer2_callback

	; The addresses of a function to call for timer 1 or timer 2 interrupts
        ; respectively. If 0 nothing is called. Callbacks happen in IRQ context
        ; and need to be short. Note: these variable must be set using a single
        ; instruction like STD, or while IRQs are inhibited to avoid race
        ; conditions. The common IRQ routine clears the interrupt, it doesn't
        ; need to happen in the callbacks.
        zp_var sound_timer1_callback,2
        zp_var sound_timer2_callback,2

sound_init:
        ldd #$0000
        std sound_timer1_callback
        std sound_timer2_callback

        ; Enable OPL3 mode globally
        lda #$05
        sta OPL3_ADDRESS_ARRAY1
        lda #$01
        sta OPL3_DATA_WRITE

        lda #18
.silence_loop:
        jsr sound_stop_note
        dec a
        bne .silence_loop

        rts

; The YMF262 datasheet does not specify whether the two timers are on the same
; clock, just that T2 increments every 320uS and T1 every 80uS.  If they can be
; set independently there is a tiny race condition between reading the status
; flag and clearing the interrupt because all flags are cleared at the same
; time. If a timer overflow happens to fall between the status read and RST
; write it gets dropped. It's probably a reasonable assumption that the tick
; source is shared and T2 just goes up every 4 T1 beats. If that's the case
; we're safe as long as we get to the clear below in less than 80uS from when
; the IRQ fires. That seems realistic as long as we don't have long SWI or NMI
; interrupt handlers.
sound_irq:
        clc
        lda OPL3_STATUS_READ
        ; lda sets N to the value of bit 7, which is also the IRQ flag. Unset
        ; means it's not an OPL3 interrupt.
        bpl .done

        ; Clear the interrupt.
        ldb #OPL3_TIMER_CONTROL
        stb OPL3_ADDRESS_ARRAY0
        ldb #$80                ; Note: touching RST does not affect the rest of
        stb OPL3_DATA_WRITE     ;       the register.


        ; Run timer 1 callback if needed
        bit a,#$40              ; Bit 6 is the timer 1 overflow flag
        beq .test_timer2

        ldx sound_timer1_callback
        beq .test_timer2
        psh a                   ; We could make it a requirement to keep A
                                ; intact - but this is safer.
        jsr 0,x
        pul a

.test_timer2:
        ; Run timer 2 callback if needed
        bit a,#$20              ; Bit 5 is the timer 2 overflow flag
        beq .handled

        ldx sound_timer2_callback
        beq .handled
        psh a
        jsr 0,x
        pul a

.handled:
        sec                     ; Set C to indicate we handled the IRQ
.done:
        rts

; Takes in a 2-op channel number 1..9 in A, and rewrites A to contain the
; corresponding OP1 base address
channel_to_op1_offset:
        cmp a,#3
        ble .low

        cmp a,#6
        ble .mid

        ; Channels are organized in 3 groups of 3, within a group they differ by
        ; one, across groups by the offsets below.
        add a,#($30-$28-3)
.mid:
        add a,#($28-$20-3)
.low:
        add a,#($20-1)
        rts

; Sets up an instrument on a given channel 1..9. Parameters:
; - X is the address to an 11-byte instrument definition
; - A contains the channel number
; Keeps all registers intact
sound_load_instrument:
        cmp a,#9
        bgt .sound_load_instrument_a1

.sound_load_instrument_a0:
        psh b
        psh a

        jsr channel_to_op1_offset
         ; OP1 AM/VIB/EGT/...
        ldb 0,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP2 AM/VIB/EGT/...
        add a,#3                ; OP2 slot is always offset by 3 from OP1
        ldb 5,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP1 KS/TL
        add a,#($20 - $03)      ; Offset between register banks is $20
        ldb 1,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

	; OP2 KS/TL
        add a,#3
        ldb 6,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP1 AR/DR
        add a,#($20 - $03)
        ldb 2,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP2 AR/DR
        add a,#3
        ldb 7,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP1 SL/RR
        add a,#($20 - $03)
        ldb 3,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        ; OP2 SL/RR
        add a,#3
        ldb 8,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

	; OP1 WS
        add a,#($60 - $03)      ; SL/RR is at $80+, WS at $E0+ -> delta $60
        ldb 4,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

	; OP2 WS
        add a,#3
        ldb 9,x
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE

        pul a                   ; A contains the channel number again
        tab                     ; Use B to preserve A
        add b,#($C0-1)
        stb OPL3_ADDRESS_ARRAY0
        ldb 10,x
        stb OPL3_DATA_WRITE

        pul b

        rts

.sound_load_instrument_a1:
        psh b
        psh a

        sub a,#9
        jsr channel_to_op1_offset
         ; OP1 AM/VIB/EGT/...
        ldb 0,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP2 AM/VIB/EGT/...
        add a,#3                ; OP2 slot is always offset by 3 from OP1
        ldb 5,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP1 KS/TL
        add a,#($20 - $03)      ; Offset between register banks is $20
        ldb 1,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

	; OP2 KS/TL
        add a,#3
        ldb 6,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP1 AR/DR
        add a,#($20 - $03)
        ldb 2,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP2 AR/DR
        add a,#3
        ldb 7,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP1 SL/RR
        add a,#($20 - $03)
        ldb 3,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        ; OP2 SL/RR
        add a,#3
        ldb 8,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

	; OP1 WS
        add a,#($60 - $03)      ; SL/RR is at $80+, WS at $E0+ -> delta $60
        ldb 4,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

	; OP2 WS
        add a,#3
        ldb 9,x
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE

        pul a                   ; A contains the channel number again
        tab                     ; Use B to preserve A
        add b,#($C0-1)
        stb OPL3_ADDRESS_ARRAY1
        ldb 10,x
        stb OPL3_DATA_WRITE

        pul b

        rts


; Sets the channel in A to the MIDI note number in B and plays it. Does not
; require the note to be stopped before. The note needs to be between 21 and 104
; inclusive.
sound_play_note:
        cmp a,#9
        bgt .sound_play_note_a1

.sound_play_note_a0:
        pshx
        psh a
        add a,#($B0-1)          ; KON/BLOCK/FNUM(H) register for a given channel
        psh a

        ; Stop the note that's playing
        sta OPL3_ADDRESS_ARRAY0
        clr OPL3_DATA_WRITE

        ;; Set up FNUM(L) first
        sub a,#$10              ; FNUM(L) register is $10 below KON/...
        sta OPL3_ADDRESS_ARRAY0

	; Get the value for FNUM(L)
        ldx #sound_midi_to_fnuml
        abx
        lda 0,x
        sta OPL3_DATA_WRITE

        ;; Set up KON/... register
        pul a                   ; Back to KON/... register
        sta OPL3_ADDRESS_ARRAY0

        ldx #sound_midi_to_kbfh
        abx
        lda 0,x
        sta OPL3_DATA_WRITE

        pul a
        pulx

        rts

.sound_play_note_a1:
        pshx
        psh a
        add a,#($B0-10)         ; KON/BLOCK/FNUM(H) register for a given channel
        psh a

        ; Stop the note that's playing
        sta OPL3_ADDRESS_ARRAY1
        clr OPL3_DATA_WRITE

        ;; Set up FNUM(L) first
        sub a,#$10              ; FNUM(L) register is $10 below KON/...
        sta OPL3_ADDRESS_ARRAY1

	; Get the value for FNUM(L)
        ldx #sound_midi_to_fnuml
        abx
        lda 0,x
        sta OPL3_DATA_WRITE

        ;; Set up KON/... register
        pul a                   ; Back to KON/... register
        sta OPL3_ADDRESS_ARRAY1

        ldx #sound_midi_to_kbfh
        abx
        lda 0,x
        sta OPL3_DATA_WRITE

        pul a
        pulx

        rts

; Sets the attenuation of the channel in A to the value in B.
; TODO: fix not clobbering KSL.
sound_set_attenuation:
        cmp a,#9
        bgt .sound_set_attenuation_a1
.sound_set_attenuation_a0:
        psh a
        add a,#($B0-1)         ; KON/BLOCK/FNUM(H) register for a given channel
        ; Stop the note that's playing
        sta OPL3_ADDRESS_ARRAY0
        clr OPL3_DATA_WRITE
        pul a

        ; returns OP1 offset for AM/VIB/... register
        jsr channel_to_op1_offset
        add a,#$23              ; Need OP2 KSL/TL which is $20+$3 above
        sta OPL3_ADDRESS_ARRAY0
        stb OPL3_DATA_WRITE
        rts

.sound_set_attenuation_a1:
        sub a,#9

        psh a
        add a,#($B0-1)         ; KON/BLOCK/FNUM(H) register for a given channel
        ; Stop the note that's playing
        sta OPL3_ADDRESS_ARRAY0
        clr OPL3_DATA_WRITE
        pul a

        ; returns OP1 offset for AM/VIB/... register
        jsr channel_to_op1_offset
        add a,#$23              ; Need OP2 KSL/TL which is $20+$3 above
        sta OPL3_ADDRESS_ARRAY1
        stb OPL3_DATA_WRITE
        rts

; Stops playing the note on the channel in A
sound_stop_note:
        cmp a,#9
        bgt .sound_stop_note_a1
.sound_stop_note_a0:
        psh a
        add a,#($B0-1)          ; KON/BLOCK/FNUM(H) register for a given channel
        sta OPL3_ADDRESS_ARRAY0
        clr OPL3_DATA_WRITE
        pul a
        rts

.sound_stop_note_a1:
        psh a
        add a,#($B0-10)         ; KON/BLOCK/FNUM(H) register for a given channel
        sta OPL3_ADDRESS_ARRAY1
        clr OPL3_DATA_WRITE
        pul a
        rts

        ENDSECTION sound

        endif
