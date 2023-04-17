        ifndef __macros_inc
__macros_inc = 1

; Define a zero page variable without having to keep track of their exact
; address
zp_addr set $0020
zp_var  macro name, size
oldorg set *
        org zp_addr[]
name    label *
        ds.b size
zp_addr[] set zp_addr[] + size
        org oldorg
        endm

; Reserves the next free page-aligned, page-sized block of memory starting at
; $0100
reserve_page_addr set $0100
reserve_page macro name
oldorg  set *
        org reserve_page_addr[]
name    label *
        ds.b 256
reserve_page_addr[] set reserve_page_addr[] + $0100
        org oldorg
        if reserve_page_addr[] >= $1100
          error "Too many pages reserved!"
        endif
        endm

reserve_memory_addr set $1000
reserve_memory macro name, size
oldorg set *
        org reserve_memory_addr[]
name    label *
        ds.b size
reserve_memory_addr[] set reserve_memory_addr[] + size
        org oldorg
        endm

        endif
