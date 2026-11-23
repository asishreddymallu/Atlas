# Phase 1 demo: x7 is independent, so it safely fills the load-use gap.
# The final values must match load_use_original.s.

addi x1, x0, 100
addi x2, x0, 20
sw   x2, 0(x1)
lw   x3, 0(x1)
addi x7, x0, 1
add  x6, x3, x1
