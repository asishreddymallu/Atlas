# Independent expectations for the reactivated pipeline tests.
# Correct semantics (rvss, h-nf stalls, h-f / nh-f forward ALU->ALU): plain sum.
# nh-nf (no hazard unit, no forwarding): an instruction reads its source in ID while the
# instruction 1 and 2 ahead are still in EX/MEM; the one 3 ahead writes in WB in the same
# cycle (write-first-half), so instruction i sees the value produced by i-3 (or the initial 0).
def correct(imms): return sum(imms)
def nhnf(imms):
    vals = []
    for i, imm in enumerate(imms):
        src = vals[i-3] if i >= 3 else 0
        vals.append(src + imm)
    return vals[-1]
code   = [1,2,3,4,5,6,7,8]     # pipeline_test_code.s (58f9c8d)
code_1 = [10]*12               # pipeline_test_code_1.s
for n,p in (("pipeline_test_code.s",code),("pipeline_test_code_1.s",code_1)):
    print(n, "correct", correct(p), "nh-nf", nhnf(p))
