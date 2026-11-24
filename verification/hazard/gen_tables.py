#!/usr/bin/env python3
"""Stage E6 part 1: the hazard unit's per-opcode tables against the RISC-V spec.

For every mnemonic the assembler accepts (instruction_syntax_map, plus the two
string-map-only names, which do not assemble and are listed as such), one
assembly line with distinctive fields (rd 12, rs1 9, rs2 10, rs3 11) and, from
the RISC-V unprivileged spec (RV64I, M, F, D, Zicsr), per machine word:
  - which of rs1 (19:15), rs2 (24:20), rs3 (31:27) it reads, and from which
    file: N(one), X (integer), F (floating point);
  - which file rd (11:7) is written in: N, X, F.
Pseudos are listed with the spec row of every word of their expansion, in
order. Nothing here comes from the simulator.
"""
import os, sys

N, X, F = 0, 1, 2

# ---- spec rows by class ---------------------------------------------------------
R_INT = "add sub sll slt sltu xor srl sra or and addw subw sllw srlw sraw " \
        "mul mulh mulhsu mulhu div divu rem remu mulw divw divuw remw remuw".split()
I_INT = "addi slti sltiu xori ori andi slli srli srai addiw slliw srliw sraiw".split()
LOADS = "lb lh lw ld lbu lhu lwu".split()
STORES = "sb sh sw sd".split()
BRANCHES = "beq bne blt bge bltu bgeu".split()
FP_RRR = [f"{op}.{w}" for w in "sd" for op in
          "fadd fsub fmul fdiv fsgnj fsgnjn fsgnjx fmin fmax".split()]
FP_CMP = [f"{op}.{w}" for w in "sd" for op in "feq flt fle".split()]
FP_R4 = [f"{op}.{w}" for w in "sd" for op in "fmadd fmsub fnmsub fnmadd".split()]
F_TO_X = [f"fcvt.{i}.{w}" for w in "sd" for i in ("w", "wu", "l", "lu")] + \
         ["fclass.s", "fclass.d", "fmv.x.w", "fmv.x.d"]
X_TO_F = [f"fcvt.{w}.{i}" for w in "sd" for i in ("w", "wu", "l", "lu")] + ["fmv.w.x", "fmv.d.x"]
F_TO_F1 = ["fsqrt.s", "fsqrt.d", "fcvt.s.d", "fcvt.d.s"]

CASES = []   # (mnemonic, line, [(r1, r2, r3, wr), ...])  -- one row per word


def add(mn, line, *rows):
    CASES.append((mn, line, list(rows)))


for mn in R_INT:
    add(mn, f"{mn} x12, x9, x10", (X, X, N, X))
for mn in I_INT:
    add(mn, f"{mn} x12, x9, 3", (X, N, N, X))
for mn in LOADS:
    add(mn, f"{mn} x12, 8(x9)", (X, N, N, X))
for mn in STORES:
    add(mn, f"{mn} x10, 8(x9)", (X, X, N, N))
for mn in BRANCHES:
    add(mn, f"{mn} x9, x10, here", (X, X, N, N))
add("lui", "lui x12, 5", (N, N, N, X))
add("auipc", "auipc x12, 5", (N, N, N, X))
add("jal", "jal x12, here", (N, N, N, X))
add("jalr", "jalr x12, 8(x9)", (X, N, N, X))
add("ecall", "ecall", (N, N, N, N))
add("ebreak", "ebreak", (N, N, N, N))
for mn in ("csrrw", "csrrs", "csrrc"):
    add(mn, f"{mn} x12, fflags, x9", (X, N, N, X))
for mn in ("csrrwi", "csrrsi", "csrrci"):
    add(mn, f"{mn} x12, fflags, 9", (N, N, N, X))
for mn in FP_RRR:
    add(mn, f"{mn} f12, f9, f10", (F, F, N, F))
for mn in FP_CMP:
    add(mn, f"{mn} x12, f9, f10", (F, F, N, X))
for mn in FP_R4:
    add(mn, f"{mn} f12, f9, f10, f11", (F, F, F, F))
for mn in F_TO_X:
    add(mn, f"{mn} x12, f9", (F, N, N, X))
for mn in X_TO_F:
    add(mn, f"{mn} f12, x9", (X, N, N, F))
for mn in F_TO_F1:
    add(mn, f"{mn} f12, f9", (F, N, N, F))
add("flw", "flw f12, 8(x9)", (X, N, N, F))
add("fld", "fld f12, 8(x9)", (X, N, N, F))
add("fsw", "fsw f10, 8(x9)", (X, F, N, N))
add("fsd", "fsd f10, 8(x9)", (X, F, N, N))

# ---- pseudos: the spec row of each word of the standard expansion -------------
ALU_I = (X, N, N, X)
ALU_R = (X, X, N, X)
BR = (X, X, N, N)
add("nop", "nop", ALU_I)                                  # addi x0, x0, 0
add("mv", "mv x12, x9", ALU_R)                            # this assembler: add x12, x9, x0 (legal; x0 is never a hazard)
add("not", "not x12, x9", ALU_I)                          # xori -1
add("neg", "neg x12, x9", ALU_R)                          # sub x12, x0, x9
add("negw", "negw x12, x9", ALU_R)                        # subw
add("sext.w", "sext.w x12, x9", ALU_I)                    # addiw
add("seqz", "seqz x12, x9", ALU_I)                        # sltiu 1
add("snez", "snez x12, x9", ALU_R)                        # sltu x12, x0, x9
add("sltz", "sltz x12, x9", ALU_R)                        # slt x12, x9, x0
add("sgtz", "sgtz x12, x9", ALU_R)                        # slt x12, x0, x9
for mn in ("beqz", "bnez", "blez", "bgez", "bltz", "bgtz"):
    add(mn, f"{mn} x9, here", BR)
for mn in ("bgt", "ble", "bgtu", "bleu"):
    add(mn, f"{mn} x9, x10, here", BR)
add("j", "j here", (N, N, N, X))                          # jal x0 (rd x0: written to the integer file)
add("jr", "jr x9", (X, N, N, X))                          # jalr x0, 0(x9)
add("ret", "ret", (X, N, N, X))                           # jalr x0, 0(x1)
add("call", "call here", (N, N, N, X))                    # jal ra (E3: one instruction)
add("tail", "tail here", (N, N, N, X))                    # jal x0
add("li", "li x12, 7", ALU_I)                             # addi
add("csrr", "csrr x12, fflags", (X, N, N, X))             # csrrs x12, fflags, x0
add("csrw", "csrw fflags, x9", (X, N, N, X))              # csrrw x0, fflags, x9
add("csrs", "csrs fflags, x9", (X, N, N, X))
add("csrc", "csrc fflags, x9", (X, N, N, X))
add("csrwi", "csrwi fflags, 9", (N, N, N, X))
add("csrsi", "csrsi fflags, 9", (N, N, N, X))
add("csrci", "csrci fflags, 9", (N, N, N, X))
add("la", "la x12, word", (N, N, N, X), ALU_I)            # auipc + addi (E2)

NOT_ASSEMBLED = ["fence", "fence_i", "fmv.s.x", "fmv.x.s"]


def main():
    # The assembler's own tables, written by `isa/sweep.py lists`.
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
    import vpaths
    syntax = {l.split()[0] for l in open(vpaths.out("isa", "list.txt"))}
    names = {l.strip() for l in open(vpaths.out("isa", "names.txt"))}
    covered = {c[0] for c in CASES} | set(NOT_ASSEMBLED)
    missing = (syntax | names) - covered
    extra = {c[0] for c in CASES} - (syntax | names)
    assert not missing, f"no spec row for {sorted(missing)}"
    assert not extra, f"not an assembler mnemonic: {sorted(extra)}"
    print("// GENERATED by verification/hazard/gen_tables.py - do not edit by hand.")
    print("const TableCase kTableCases[] = {")
    for mn, line, rows in sorted(CASES):
        rs = ", ".join("{%d, %d, %d, %d}" % r for r in rows)
        print(f'    {{"{mn}", "{line}", {{{rs}}}}},')
    print("};")
    print(f"// not assembled (no encoder entry): {', '.join(NOT_ASSEMBLED)}")
    print(f"{len(CASES)} mnemonics, {sum(len(r) for _, _, r in CASES)} words; "
          f"union of the maps {len(syntax | names)}", file=sys.stderr)


if __name__ == "__main__":
    main()
