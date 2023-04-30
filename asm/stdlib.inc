        ifndef __stdlib_inc
__stdlib_inc = 1

        ; monitor.inc needs to go before macros as it defines some offsets for
        ; e.g. zero page variables for user programs.
        include monitor.inc
        include include/key_constants
        include include/macros
        include include/registers


        endif
