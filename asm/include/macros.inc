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
        ifndef __stdlib_inc
        SHARED zp_addr[]
zp_addr[] set $0020
        endif
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

; Reserves a block of memory of 'size' bytes in the 'system' area of memory
; above $7e00.
reserve_system_memory_addr set $7e00
reserve_system_memory macro name, size
        orgsave
        org reserve_system_memory_addr[]
name    label *
        ds.b size
reserve_system_memory_addr[] set reserve_system_memory_addr[] + size

        if * > $7eff - 64       ; Probably way more than needed
          error "Not enough space for monitor stack left"
        endif

        orgrestore
        endm

PUBLIC_SHARED macro name
        PUBLIC name
        SHARED name
        endm

        endif
