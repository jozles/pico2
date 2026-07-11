.syntax unified
.thumb
.thumb_func
.cpu cortex-m33

.global blank_
blank_:
    push    {r4-r7, lr}

    @ r0 = ptr
    @ r1 = len24
    @ r2 = fill_byte (0..255)

    @ Masquage 24 bits
    ands    r1, r1, #0x00FFFFFF
    cbz     r1, end

    @ Construire un mot 32 bits : fill32 = byte * 0x01010101
    @ r3 = fill_byte
    mov     r3, r2
    orr     r3, r3, r3, lsl #8
    orr     r3, r3, r3, lsl #16

    @ --- boucle 32 octets ---
    lsrs    r12, r1, #5        @ r12 = len / 32
loop32:
    subs    r12, r12, #1
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    str     r3, [r0], #4
    bne     loop32

    @ --- mots 32 bits ---
    ands    r1, r1, #31
    lsrs    r12, r1, #2
loop4:
    subs    r12, r12, #1
    str     r3, [r0], #4
    bne     loop4

    @ --- octets ---
    ands    r1, r1, #3
    cbz     r1, end
byte_loop:
    strb    r2, [r0], #1
    subs    r1, r1, #1
    bne     byte_loop

end:
    pop     {r4-r7, pc}
