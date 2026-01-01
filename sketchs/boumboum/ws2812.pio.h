.program ws2812
.side_set 1

; PIO clock typique : 8 MHz → 125 ns par cycle
; Timings WS2812 :
;   1 : haut ~700 ns, bas ~600 ns
;   0 : haut ~350 ns, bas ~800 ns

.wrap_target
pull block         ; récupère 32 bits
out x, 8           ; ignore les 8 MSB → ne garde que 24 bits
bitloop:
    out y, 1       ; récupère 1 bit dans Y
    jmp !y do_zero side 1 [2] ; bit=1 → HIGH plus longtemps
do_one:
    jmp continue   side 0 [4]
do_zero:
    jmp continue   side 0 [1]
continue:
    jmp x-- bitloop
.wrap