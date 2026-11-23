# Phase 1 demo: a load is immediately consumed, creating a load-use stall.
# Compare this file with load_use_reordered.s using the five-stage processor.

addi x1, x0, 100
addi x2, x0, 20
sw   x2, 0(x1)
lw   x3, 0(x1)
add  x6, x3, x1
addi x7, x0, 1
