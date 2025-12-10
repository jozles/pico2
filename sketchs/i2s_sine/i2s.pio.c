.program i2s
.side_set 2 opt    ; side[0]=BCLK, side[1]=LRCLK
; pins OUT: DIN

pull block

; MSB delay gauche: 1 bit (2 instr)
nop        side 0b01   ; BCLK=0, LRCLK=1 (gauche)
nop        side 0b00   ; BCLK=1, LRCLK=0 (gauche)

set x, 15
left_bits:
    out pins, 1 side 0b01  ; BCLK=0, LRCLK=gauche
    nop         side 0b00  ; BCLK=1
    jmp x-- left_bits

; MSB delay droit: 1 bit (2 instr)
nop        side 0b11   ; BCLK=0, LRCLK=1 (droit)
nop        side 0b10   ; BCLK=1, LRCLK=1 (droit)

set x, 15
right_bits:
    out pins, 1 side 0b11  ; BCLK=0, LRCLK=droit
    nop         side 0b10  ; BCLK=1
    jmp x-- right_bits

jmp 0
