# Phase 1 safety demo: the final write to x2 must NOT move above the add,
# because the add reads the old x2 value (a WAR / anti-dependency).

addi x1, x0, 100
lw   x5, 0(x1)
add  x6, x5, x2
addi x2, x3, 1
