# Phase 1 safety demo: the load and store order is preserved by a memory edge.

lw   x5, 0(x1)
sw   x2, 0(x1)
add  x6, x5, x3
