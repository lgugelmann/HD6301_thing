        ifndef __sound_inc
__sound_inc = 1

        include macros
        include registers

        SECTION sound
        PUBLIC sound_init
        PUBLIC sound_irq

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

        ENDSECTION sound

        endif
