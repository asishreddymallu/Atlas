    .data
nums:
    .word 1, 2, 3
    .text
main:
    la   t0, nums
    lw   t1, 0(t0)
