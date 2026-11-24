#!/usr/bin/env python3
"""Stage E4 part 4 (known_issues 104): Zicsr on fflags / frm / fcsr.

Oracle: a Python model of the RISC-V fcsr, written from the spec
(unprivileged ISA, "Zicsr" and F "Floating-Point Control and Status
Register"): fflags = fcsr[4:0], frm = fcsr[7:5], fcsr bits 31:8 read 0.
csrrw/csrrwi always write and read unless rd = x0 (then no read);
csrrs/csrrc(i) always read and write only if rs1 (or uimm) is not x0/0;
rd gets the OLD value. Each result is stored to scratch right after the
CSR instruction, so in the unpadded variant the store is a younger
instruction that must see rd.
"""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from common import *

ADDR = {"fflags": 1, "frm": 2, "fcsr": 3}


class Fcsr:
    def __init__(self): self.v = 0

    def read(self, c):
        return {1: self.v & 0x1F, 2: (self.v >> 5) & 7, 3: self.v & 0xFF}[ADDR[c]]

    def write(self, c, x):
        a = ADDR[c]
        if a == 1: self.v = (self.v & ~0x1F) | (x & 0x1F)
        elif a == 2: self.v = (self.v & ~0xE0) | ((x & 7) << 5)
        else: self.v = x & 0xFF

    def op(self, form, c, src_val, src_is_zero_reg, rd_is_x0):
        old = self.read(c)
        kind = form.rstrip("i")[-1]  # w / s / c
        if kind == "w":
            self.write(c, src_val)
        elif not src_is_zero_reg:
            self.write(c, old | src_val if kind == "s" else old & ~src_val)
        return old


class Gen:
    """A program builder that tracks the x registers and the fcsr model."""

    def __init__(self):
        self.p, self.m, self.dw, self.k, self.x = Program(), Fcsr(), {}, 0, {0: 0}

    def setx(self, r, v):
        self.p.load(f"x{r}", v); self.x[r] = u64(v)

    def csr(self, form, rd, c, src, store=True):
        if form.endswith("i"):
            old = self.m.op(form, c, src, src == 0, rd == 0)
            self.p.ins(f"{form} x{rd}, {c}, {src}")
        else:
            old = self.m.op(form, c, self.x[src], src == 0, rd == 0)
            self.p.ins(f"{form} x{rd}, {c}, x{src}")
        if rd != 0:
            self.x[rd] = old
        if store:
            self.store(rd)

    def store(self, r):
        self.p.ins(f"sd x{r}, {8 * self.k}(x11)")
        self.dw[SCRATCH + 8 * self.k] = self.x[r]; self.k += 1

    def read_all(self):
        for c in ("fflags", "frm", "fcsr"):
            self.csr("csrrs", 7, c, 0)


progs = {}
# A: the six forms on each CSR, from several starting values
for c in ("fflags", "frm", "fcsr"):
    g = Gen()
    g.setx(6, 0xA5); g.setx(8, 0xFFFFFFFFFFFFFFFF); g.setx(9, 0x5A)
    for start in (0x00, 0xFF):
        g.csr("csrrw", 0, "fcsr", 8 if start else 0)  # fcsr <- start
        for form, src in (("csrrw", 6), ("csrrs", 9), ("csrrc", 6), ("csrrs", 8), ("csrrc", 8),
                          ("csrrwi", 21), ("csrrsi", 10), ("csrrci", 5), ("csrrwi", 0),
                          ("csrrsi", 31), ("csrrci", 31)):
            g.csr(form, 5, c, src)
            g.read_all()
    progs[f"SixForms_{c}"] = g
# B: rd = x0 / rs1 = x0 / uimm 0 rules
g = Gen()
g.setx(6, 0x7F); g.setx(5, 0x1234)
g.csr("csrrw", 0, "fcsr", 6)        # rd = x0: writes, x0 stays 0
g.store(0)
g.csr("csrrs", 5, "fcsr", 0)        # reads, no write
g.csr("csrrc", 5, "fflags", 0)      # reads, no write
g.csr("csrrsi", 5, "frm", 0)        # reads, no write
g.csr("csrrci", 5, "fcsr", 0)       # reads, no write
g.csr("csrrs", 0, "fcsr", 6)        # rd = x0 but rs1 != x0: writes
g.csr("csrrci", 0, "fcsr", 31)
g.read_all()
progs["ZeroRegisterRules"] = g
# C: aliasing
g = Gen()
g.setx(6, 0x1FF); g.setx(9, 0x3)
g.csr("csrrwi", 0, "frm", 3); g.read_all()
g.csr("csrrw", 0, "fcsr", 6); g.read_all()   # 0x1FF masks to 0xFF
g.csr("csrrwi", 0, "fflags", 0); g.read_all()
g.csr("csrrw", 0, "frm", 6); g.read_all()    # frm takes 3 bits
g.csr("csrrs", 0, "fflags", 9); g.read_all()
g.csr("csrrci", 0, "frm", 5); g.read_all()
progs["Aliasing"] = g

fb = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]
db = lambda x: struct.unpack("<Q", struct.pack("<d", x))[0]
# D: a frm write changes the mode of the FP op after it, not the one before
for fmt in ("s", "d"):
    g = Gen()
    one, tiny = (fb(1.0), fb(2.0 ** -24)) if fmt == "s" else (db(1.0), db(2.0 ** -53))
    up = one + 1                    # 1 + 1 ulp: RUP of an exact tie
    ld = "flw" if fmt == "s" else "fld"
    g.p.ins(f"{ld} f1, {g.p.value(one)}(x10)", f"{ld} f2, {g.p.value(tiny)}(x10)")
    g.p.ins(f"fadd.{fmt} f3, f1, f2")          # frm = 0: RNE, ties to even -> 1.0
    g.csr("csrrwi", 5, "frm", 3)               # frm <- RUP; x5 = old frm 0
    g.p.ins(f"fadd.{fmt} f4, f1, f2")          # RUP -> 1 + ulp
    g.csr("csrrwi", 5, "frm", 1)               # RTZ
    g.p.ins(f"fadd.{fmt} f5, f1, f2", f"fadd.{fmt} f6, f1, f2, rup")
    g.fexp = {3: one, 4: up, 5: one, 6: up}
    progs[f"FrmOrdering_{fmt}"] = g
# E: a flags read is not overtaken by a younger FP op (it must be flushed)
g = Gen()
g.p.ins(f"flw f1, {g.p.value(fb(1.0))}(x10)", f"flw f2, {g.p.value(0)}(x10)")
g.csr("csrrwi", 0, "fcsr", 0)
g.csr("csrrs", 5, "fflags", 0, store=False) # must read 0 ...
g.p.ins("fdiv.s f3, f1, f2")                # ... although this raises DZ directly behind it
g.store(5)                                  # (in EX while the read is in MEM)
g.csr("csrrwi", 0, "fflags", 0)             # wipes whatever it raised (72: value not modelled)
g.csr("csrrs", 7, "fflags", 0)
progs["YoungerFpOpDoesNotLeak"] = g

cases = []
for padded in (True, False):
    for n, g in progs.items():
        name = n + ("" if padded else "_Unpadded")
        f = getattr(g, "fexp", None)
        cases.append((name, padded, cpp_case(name, g.p.text(padded), dwords=g.dw, f=f)))
print("// GENERATED by verification/tables/gen_csr.py - do not edit by hand.")
print("const IsaCase kPadded[] = {")
print("\n".join(c for _, p, c in cases if p))
print("};")
print("const IsaCase kUnpadded[] = {")
print("\n".join(c for _, p, c in cases if not p))
print("};")
print(sum(len(g.dw) for g in progs.values()), "stored results per variant,", len(progs), "programs", file=sys.stderr)
