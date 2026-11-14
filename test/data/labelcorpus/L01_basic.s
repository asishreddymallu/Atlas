main:
    addi x1, x0, 4
loop:
    addi x1, x1, -1
    bne x1, x0, loop
    ret
