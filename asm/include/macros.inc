        ifndef __macros_inc
__macros_inc = 1

; Define a zero page variable without having to keep track of their exact
; address
zp_addr set $001f
zp_var  macro name, size
oldorg set *
        org zp_addr
name    label *   
        db.b size
zp_addr set zp_addr+size
        org oldorg
        endm

        endif
