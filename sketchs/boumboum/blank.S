    .syntax unified
    .thumb
    .thumb_func
    .cpu cortex-m33
    
    .global blank
blank:
    push    {r4,r5,r6,lr}

    cbz     r1, end

align_loop:
    tst     r0, #3
    beq     aligned
    movs    r2, #0
    strb    r2, [r0], #1
    subs    r1, r1, #1
    beq     end
    b       align_loop

aligned:
    movs    r2, #0
    mov     r3, r2
    mov     r4, r2
    mov     r5, r2

    lsrs    r6, r1, #4
    beq     tail_bytes

word_loop:
    stm     r0!, {r2,r3,r4,r5}
    subs    r6, r6, #1
    bne     word_loop

tail_bytes:
    ands    r1, r1, #0x0F
    beq     end

byte_loop:
    strb    r2, [r0], #1
    subs    r1, r1, #1
    bne     byte_loop

end:
    pop     {r4,r5,r6,pc}