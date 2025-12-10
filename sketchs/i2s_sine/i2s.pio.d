.program i2s
.side_set 2 opt    ; side[0]=BCLK, side[1]=LRCLK
; OUT pin = DIN

pull block

; canal gauche
set x, 15
left_bits:
    out pins, 1 side 0b01   ; BCLK=0, LRCLK=gauche → DIN change ici
    nop         side 0b00   ; BCLK=1, LRCLK=gauche → codec lit DIN
    jmp x-- left_bits

; canal droit
set x, 15
right_bits:
    out pins, 1 side 0b11   ; BCLK=0, LRCLK=droit → DIN change ici
    nop         side 0b10   ; BCLK=1, LRCLK=droit → codec lit DIN
    jmp x-- right_bits

jmp 0
