.program i2s
.side_set 2 opt

; Programme minimal I²S : envoie un mot 32 bits (16 bits gauche + 16 bits droit)
; LRCLK bas = gauche, haut = droite
; BCLK généré par side_set

; Récupère un mot 32 bits du FIFO
pull block

; Canal gauche (LRCLK=0), délai 1 bit avant MSB

set x, 31
left_preamble:
    nop       side 0b00     ; BCLK=0, LRCLK=0 (1er demi-cycle, délai)
    nop       side 0b01     ; BCLK=1, LRCLK=0 (2e demi-cycle, délai)

left_loop:
set y, 15
    out pins, 1 side 0b00   ; LRCLK=0, envoie bit sur DIN
    nop         side 0b01     ; toggle BCLK
    jmp y-- left_loop

; Canal droit (LRCLK=1), délai 1 bit avant MSB
right_preamble:
    nop       side 0b10     ; BCLK=0, LRCLK=1 (1er demi-cycle, délai)
    nop       side 0b11     ; BCLK=1, LRCLK=1 (2e demi-cycle, délai)

set y, 15
right_loop:
    out pins, 1 side 0b10   ; LRCLK=1, envoie bit sur DIN
    nop         side 0b11     ; toggle BCLK
    jmp y-- right_loop

; Boucle infinie
jmp 0

.wrap
