        org $0000
start:
        ; Some dummy code
        lda .data
        jmp next

.data:
        byt "This is data, not code\0"

next:
        ; Some more dummy code
        lda #$01
        jmp start