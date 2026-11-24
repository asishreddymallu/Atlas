.data
msg:    .asciiz "hi"
val:    .word 1, 2, 3
.text
main:
        xyzzy   x1, x2, x3
        addi    x1, x99, 4
        addi    x1, x2
        addi    x1, x2, 99999
        lw      x5, notalabel
        jal     missing_target
        add     x1, x2, x3, x4
        .unknowndirective 7
        sw      x1, 0(x2
        beq     x1, x2, undefined_lbl
