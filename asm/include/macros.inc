        ifndef __macros_inc
__macros_inc = 1

; orgsave/orgrestore save and restore the current program address. Saves /
; restores can't be nested.
orgsave macro
oldorg[] set *
        endm

orgrestore macro
        org oldorg[]
        endm

; Define a zero page variable without having to keep track of their exact
; address
zp_addr set $0020
zp_var  macro name, size
        orgsave
        org zp_addr[]
name    label *
        ds.b size
zp_addr[] set zp_addr[] + size
        orgrestore
        endm

; Reserves the next free page-aligned, page-sized block of memory starting at
; $0100
reserve_page_addr set $0100
reserve_page macro name
        orgsave
        org reserve_page_addr[]
name    label *
        ds.b 256
reserve_page_addr[] set reserve_page_addr[] + $0100
        orgrestore
        if reserve_page_addr[] >= $1100
          error "Too many pages reserved!"
        endif
        endm

; Reserves a block of memory of 'size' bytes. Starts at $1000 to avoid
; conflicting with page-sized blocks of memory.
reserve_memory_addr set $1000
reserve_memory macro name, size
        orgsave
        org reserve_memory_addr[]
name    label *
        ds.b size
reserve_memory_addr[] set reserve_memory_addr[] + size
        orgrestore
        endm

        endif
