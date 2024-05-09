        ifndef __macros_inc
__macros_inc = 1

        include memory_map

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
zp_addr[] set ZP_START
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
        if reserve_memory_addr[] >= SYSTEM_MEMORY_START - MIN_USER_STACK_SIZE
          error "Can't reserve memory, ran into stack!"
        endif
        endm

; Reserves a block of memory of 'size' bytes in the 'system' area of memory
; above $7e00.
reserve_system_memory_addr set SYSTEM_MEMORY_START
reserve_system_memory macro name, size
        orgsave
        org reserve_system_memory_addr[]
name    label *
        ds.b size
reserve_system_memory_addr[] set reserve_system_memory_addr[] + size

        if * > MONITOR_STACK_START - 64 ; Probably way more than needed
          error "Not enough space for monitor stack left"
        endif

        orgrestore
        endm

nop10   macro
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        endm

PUBLIC_SHARED macro name
        PUBLIC name
        SHARED name
        endm

        endif
