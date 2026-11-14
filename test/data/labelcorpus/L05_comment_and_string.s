.data
msg:
    .asciz "inside: not a label"
.text
real:
    nop        # fake: not a label
    j real
