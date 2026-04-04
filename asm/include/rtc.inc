        ifndef __rtc_inc
__rtc_inc = 1

        cpu 6301

        ; MCP7940N RTC connected via I2C on W65C22 Port A.
        ; SCL = Port A bit 0, SDA = Port A bit 1 (same as i2c.inc).

        SECTION rtc
        PUBLIC rtc_init
        PUBLIC rtc_get_time
        PUBLIC rtc_seconds
        PUBLIC rtc_minutes
        PUBLIC rtc_hours

        ; MCP7940N 7-bit I2C address is 0x6F.
RTC_ADDRESS     = $6F
RTC_ADDR_WRITE  = RTC_ADDRESS << 1       ; 0xDE
RTC_ADDR_READ   = RTC_ADDRESS << 1 | 1  ; 0xDF

        ; Register addresses
RTC_REG_SEC     = $00   ; RTCSEC:   bit7=ST (osc start), BCD seconds
RTC_REG_MIN     = $01   ; RTCMIN:   BCD minutes
RTC_REG_HOUR    = $02   ; RTCHOUR:  bit6=12/24 mode, BCD hours
RTC_REG_WKDAY   = $03   ; RTCWKDAY: bit5=OSCRUN, bit3=VBATEN, day 1-7
RTC_REG_DATE    = $04   ; RTCDATE:  BCD day-of-month
RTC_REG_MTH     = $05   ; RTCMTH:   BCD month
RTC_REG_YEAR    = $06   ; RTCYEAR:  BCD year (00-99)

        ; Scratch memory for time values filled in by rtc_get_time.
        reserve_system_memory rtc_seconds, 1
        reserve_system_memory rtc_minutes, 1
        reserve_system_memory rtc_hours,   1

; Initializes the MCP7940N: enables the oscillator (ST bit) and VBATEN.
; Clobbers A, B, X.
rtc_init:
        ; Write RTCSEC (0x00) = 0x80  (ST=1, seconds=00)
        jsr i2c_start
        lda #RTC_ADDR_WRITE
        jsr i2c_write_byte
        lda #RTC_REG_SEC
        jsr i2c_write_byte
        lda #$80                ; ST=1, BCD seconds = 00
        jsr i2c_write_byte
        jsr i2c_stop

        ; Write RTCWKDAY (0x03) = 0x08  (VBATEN=1)
        jsr i2c_start
        lda #RTC_ADDR_WRITE
        jsr i2c_write_byte
        lda #RTC_REG_WKDAY
        jsr i2c_write_byte
        lda #$08                ; VBATEN=1
        jsr i2c_write_byte
        jsr i2c_stop
        rts

; Reads the current time from the MCP7940N into rtc_seconds, rtc_minutes,
; rtc_hours (all in BCD). Clobbers A, B, X.
rtc_get_time:
        ; Step 1: set the register pointer to 0x00.
        jsr i2c_start
        lda #RTC_ADDR_WRITE
        jsr i2c_write_byte
        lda #RTC_REG_SEC
        jsr i2c_write_byte
        jsr i2c_stop

        ; Step 2: sequential read of sec, min, hour.
        jsr i2c_start
        lda #RTC_ADDR_READ
        jsr i2c_write_byte
        lda #0                  ; ACK (more bytes to follow)
        jsr i2c_read_byte
        and a,#$7f              ; mask out the ST bit
        sta rtc_seconds
        lda #0                  ; ACK
        jsr i2c_read_byte
        sta rtc_minutes
        lda #1                  ; NACK (last byte)
        jsr i2c_read_byte
        and a,#$3f              ; mask out the 12/24-hr mode bit
        sta rtc_hours
        jsr i2c_stop
        rts

        ENDSECTION rtc

        endif
