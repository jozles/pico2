.syntax unified
.thumb
.thumb_func
.cpu cortex-m33

.global blank
blank_:
    push    {r4-r9, lr}

    @ r0 = ptr
    @ r1 = len24
    @ r2 = fill value (byte or word)

    lsls    r1, r1, #8     @ décaler à gauche
    lsrs    r1, r1, #8     @ redécaler à droite → top 8 bits = 0

    cbz     r1, end

    @ --- préparer les registres r2..r9 avec la valeur de remplissage ---
    mov     r3, r2
    mov     r4, r2
    mov     r5, r2
    mov     r6, r2
    mov     r7, r2
    mov     r8, r2
    mov     r9, r2

    @ --- boucle 32 octets ---
    lsrs    r12, r1, #5        @ r12 = len / 32
loop32:
    cmp     r12, #0
    beq     tail_words
    stm     r0!, {r2-r9}
    subs    r12, r12, #1
    b       loop32

tail_words:
    ands    r1, r1, #31
    lsrs    r12, r1, #2        @ mots 32 bits
loop4:
    cmp     r12, #0
    beq     tail_bytes
    str     r2, [r0], #4
    subs    r12, r12, #1
    b       loop4

tail_bytes:
    ands    r1, r1, #3
    cbz     r1, end
byte_loop:
    strb    r2, [r0], #1
    subs    r1, r1, #1
    bne     byte_loop

end:
    pop     {r4-r9, pc}