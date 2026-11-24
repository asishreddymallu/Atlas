#!/usr/bin/env python3
"""Lays out the pc_relative_test programs and computes every expectation
independently: an instruction's address is TEXT_BASE + 4 * index, a data
label's is DATA_BASE + offset, branch outcomes use Python's own signed and
unsigned comparison. Nothing is read back from any simulator model.

Writes the C++ program/expectation table to stdout."""
TEXT_BASE, DATA_BASE = 0, 0x10000000
M64 = (1 << 64) - 1
NOP = "nop"
GAP = [NOP] * 4

def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v

class Prog:
    def __init__(self, name, owe):
        self.name, self.owe, self.lines, self.labels, self.data = name, owe, [], {}, []
        self.expect = {}
    def ins(self, *xs):          # each entry is ONE machine instruction
        for x in xs:
            self.lines.append(("i", x))
    def la(self, rd, label):     # la expands to auipc + addi: two instructions
        self.lines.append(("la", f"la {rd}, {label}"))
    def label(self, name):
        self.labels[name] = self.index()
        self.lines.append(("l", name))
    def index(self):
        return sum(2 if k == "la" else 1 for k, _ in self.lines if k != "l")
    def addr(self, label):
        return TEXT_BASE + 4 * self.labels[label]
    def text(self):
        out = []
        if self.data:
            out.append(".data")
            out += self.data
        out.append(".text")
        for k, x in self.lines:
            out.append(f"{x}:" if k == "l" else f"    {x}")
        return "\n".join(out) + "\n"

progs = []

# ---- auipc at several PCs: first, after a no-dependency run, after a taken branch
p = Prog("AuipcAtSeveralPcs", "padded")
p.data = ["w: .dword 0x1122334455667788", "v: .dword 7"]
i = p.index(); p.ins("auipc x5, 0");       p.expect[5] = TEXT_BASE + 4 * i
i = p.index(); p.ins("auipc x6, 1");       p.expect[6] = TEXT_BASE + 4 * i + (1 << 12)
i = p.index(); p.ins("auipc x7, 0xfffff"); p.expect[7] = (TEXT_BASE + 4 * i + sx(0xFFFFF << 12, 32)) & M64
p.ins("beq x0, x0, after_branch", "addi x8, x0, 1"); p.expect[8] = 0
p.label("after_branch")
i = p.index(); p.ins("auipc x9, 0");       p.expect[9] = TEXT_BASE + 4 * i
p.ins(*GAP)
progs.append(p)

# ---- la of a .data label: auipc + addi, a chain the assembler emits with
# no gap, so (like li) nh_nf cannot run it whatever the source does --------
p = Prog("LaOfADataLabel", "chain")
p.data = ["w: .dword 0x1122334455667788", "v: .dword 7"]
p.la("x11", "v"); p.ins(*GAP);              p.expect[11] = DATA_BASE + 8
p.ins("ld x12, 0(x11)", *GAP);             p.expect[12] = 7
p.la("x13", "w"); p.ins(*GAP);              p.expect[13] = DATA_BASE
p.ins("ld x14, 0(x13)", *GAP);             p.expect[14] = 0x1122334455667788
progs.append(p)

# ---- jal / ret / j / jr / jalr link values and targets ---------------------
p = Prog("JumpsLinkAndLand", "padded")
i = p.index(); p.ins("jal x1, f");          p.expect[1] = TEXT_BASE + 4 * (i + 1)
p.ins("addi x21, x0, 21");                  p.expect[21] = 21     # reached by ret
p.ins("j end", "addi x22, x0, 22");         p.expect[22] = 0      # skipped by j
p.label("f")
p.ins("addi x20, x0, 20", *GAP, "ret");     p.expect[20] = 20
p.label("end")
# jr: target = auipc + K, K computed from the layout
i_auipc = p.index(); p.ins("auipc x6, 0", *GAP)
i_addi = p.index(); p.ins("ADDI_X6", *GAP)
p.ins("jr x6", "addi x23, x0, 23");         p.expect[23] = 0      # skipped by jr
p.label("jr_target")
p.ins("addi x24, x0, 24");                  p.expect[24] = 24
k = p.addr("jr_target") - (TEXT_BASE + 4 * i_auipc)
p.lines[[n for n, (kk, x) in enumerate(p.lines) if x == "ADDI_X6"][0]] = ("i", f"addi x6, x6, {k}")
p.expect[6] = p.addr("jr_target")
# jalr with a link register
i_auipc = p.index(); p.ins("auipc x7, 0", *GAP)
p.ins("ADDI_X7", *GAP)
i_jalr = p.index(); p.ins("jalr x5, 0(x7)", "addi x25, x0, 25")
p.expect[5] = TEXT_BASE + 4 * (i_jalr + 1); p.expect[25] = 0
p.label("jalr_target")
p.ins("addi x26, x0, 26", *GAP);            p.expect[26] = 26
k = p.addr("jalr_target") - (TEXT_BASE + 4 * i_auipc)
p.lines[[n for n, (kk, x) in enumerate(p.lines) if x == "ADDI_X7"][0]] = ("i", f"addi x7, x7, {k}")
p.expect[7] = p.addr("jalr_target")
progs.append(p)

# ---- all six branches: taken/not taken forward, taken backward -------------
p = Prog("SixBranches", "padded")
regs = {"x10": 1, "x11": 2, "x12": -1}
p.ins("addi x10, x0, 1", "addi x11, x0, 2", "addi x12, x0, -1", *GAP)
def cond(op, a, b):
    ua, ub = a & M64, b & M64
    return {"beq": a == b, "bne": a != b, "blt": a < b, "bge": a >= b,
            "bltu": ua < ub, "bgeu": ua >= ub}[op]
forward = [("beq", "x10", "x10"), ("beq", "x10", "x11"), ("bne", "x10", "x11"), ("bne", "x10", "x10"),
           ("blt", "x12", "x10"), ("blt", "x10", "x12"), ("bge", "x10", "x12"), ("bge", "x12", "x10"),
           ("bltu", "x10", "x12"), ("bltu", "x12", "x10"), ("bgeu", "x12", "x10"), ("bgeu", "x10", "x12")]
marker = 13
for n, (op, a, b) in enumerate(forward):
    lab = f"fwd{n}"
    p.ins(f"{op} {a}, {b}, {lab}", f"addi x{marker}, x0, 1")
    p.label(lab)
    p.expect[marker] = 0 if cond(op, regs[a], regs[b]) else 1
    marker += 1
assert marker <= 25
# backward: counter c starts at 0, incremented, branch back while the
# condition holds; Python runs the same loop to get the final count.
backward = [("beq", "C", "x10"), ("bne", "C", "x11"), ("blt", "C", "x11"),
            ("bge", "x10", "C"), ("bltu", "C", "x11"), ("bgeu", "x10", "C")]
c = 25
for n, (op, a, b) in enumerate(backward):
    reg = f"x{c}"
    p.ins(f"addi {reg}, x0, 0", *GAP)
    p.label(f"back{n}")
    p.ins(f"addi {reg}, {reg}, 1", *GAP)
    p.ins(f"{op} {a.replace('C', reg)}, {b.replace('C', reg)}, back{n}")
    v = 0
    while True:
        v += 1
        env = dict(regs, C=v)
        if not cond(op, env[a] if a != "C" else v, env[b] if b != "C" else v):
            break
    p.expect[c] = v
    c += 1
p.ins(*GAP)
progs.append(p)

# ---- jalr after a producer at distance 0, 1, 2 ------------------------------
for kind in ("alu", "load"):
    for d in (0, 1, 2):
        p = Prog(f"JalrAfter{kind.capitalize()}Distance{d}", f"{kind}:{d}")
        p.ins("lui x10, 0x10000", *GAP)              # data base
        p.ins("ADDI_X7", *GAP, "sd x7, 0(x10)", *GAP)
        p.ins("ld x6, 0(x10)" if kind == "load" else "ADDI_X6")
        p.ins(*([NOP] * d))
        i_jalr = p.index(); p.ins("jalr x1, 0(x6)", "addi x28, x0, 28")
        p.label("target")
        p.ins("addi x29, x0, 29", *GAP)
        t = p.addr("target")
        p.lines = [("i", f"addi x7, x0, {t}") if x == "ADDI_X7" else
                   ("i", f"addi x6, x0, {t}") if x == "ADDI_X6" else (k_, x) for k_, x in p.lines]
        p.expect.update({1: TEXT_BASE + 4 * (i_jalr + 1), 6: t, 28: 0, 29: 29})
        progs.append(p)

# ---- emit -------------------------------------------------------------------
print("// GENERATED by verification/tables/gen_pcrel.py - see the file comment.")
print("const PcCase kCases[] = {")
for p in progs:
    exp = ", ".join(f"{{{r}, 0x{v & M64:X}ULL}}" for r, v in sorted(p.expect.items()))
    print(f'    {{"{p.name}", "{p.owe}",\nR"ASM({p.text()})ASM",\n     {{{exp}}}}},')
print("};")
