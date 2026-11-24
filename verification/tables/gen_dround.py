#!/usr/bin/env python3
"""Stage E3 part D (known_issues 107): double-precision rounding.

kDyn   - fadd/fsub/fmul/fdiv/fsqrt.d, fcvt.l.d, fcvt.d.l with the default
         (dynamic) rounding mode and frm = 0, each after an fld - the load
         whose funct3 left the host in round-up. Oracle: Python's own IEEE
         double arithmetic, which is correctly rounded to nearest-even.
kModes - fadd.d, fmul.d, fcvt.l.d with explicit rtz/rdn/rup/rmm. Oracle:
         the exact result as a Fraction, rounded to a double (or an
         integer) by the mode's own rule. RMM is asserted only where the
         exact result is NOT a tie; ties are printed separately (the ALU has
         no RMM and falls back to the host mode - known_issues 113).
"""
import math, os, struct, sys
from fractions import Fraction
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from common import *

D = lambda x: struct.unpack("<Q", struct.pack("<d", x))[0]
B = lambda q: struct.unpack("<d", struct.pack("<Q", q))[0]
TINY = B(1)                       # the smallest denormal
HALF_ULP_1 = 2.0 ** -53           # 1 + this is an exact tie


def round_double(q, mode):
    """Round the exact rational q to a double under mode (finite results only)."""
    d = float(q)                  # correctly rounded, nearest-even
    if Fraction(d) == q:
        return d
    lo, hi = (d, math.nextafter(d, math.inf)) if Fraction(d) < q else (math.nextafter(d, -math.inf), d)
    if mode == "rtz":
        return lo if q > 0 else hi
    if mode == "rdn":
        return lo
    if mode == "rup":
        return hi
    if mode == "rmm":
        dl, dh = q - Fraction(lo), Fraction(hi) - q
        if dl == dh:
            return hi if q > 0 else lo
        return lo if dl < dh else hi
    return d


def round_int(q, mode):
    f = math.floor(q)
    frac = q - f
    if mode == "rtz":
        return math.trunc(q)
    if mode == "rdn":
        return f
    if mode == "rup":
        return math.ceil(q)
    if mode == "rmm":
        if frac == Fraction(1, 2):
            return f + 1 if q > 0 else f
        return f if frac < Fraction(1, 2) else f + 1
    return round(q)                # RNE


def is_tie_double(q):
    d = float(q)
    if Fraction(d) == q:
        return False
    lo, hi = (d, math.nextafter(d, math.inf)) if Fraction(d) < q else (math.nextafter(d, -math.inf), d)
    return q - Fraction(lo) == Fraction(hi) - q


def program(loads, op):
    """fld every operand (the leak trigger), then the op."""
    p = Program()
    for reg, bits in loads:
        if reg.startswith("f"):
            p.ins(f"fld {reg}, {p.value(bits)}(x10)")
        else:
            p.load(reg, bits)
    p.ins(op)
    return p.text()


dyn, modes, ties = [], [], []
PAIRS = [(1.0, TINY), (-1.5, TINY), (0.1, 0.2), (1.0, HALF_ULP_1), (1.0, 3.0), (2.0 / 3.0, 1e-17),
         (1e308, 1e292), (-0.1, 0.7), (3.0, 7.0), (1.0 + 2 ** -52, 1.0 + 2 ** -52)]
OPS = {"fadd.d": lambda a, b: a + b, "fsub.d": lambda a, b: a - b,
       "fmul.d": lambda a, b: a * b, "fdiv.d": lambda a, b: a / b}
for mn, f in OPS.items():
    for k, (a, b) in enumerate(PAIRS):
        dyn.append(cpp_case(f"{mn}Dyn{k}", program([("f1", D(a)), ("f2", D(b))], f"{mn} f3, f1, f2"),
                            f={3: D(f(a, b))}))
for k, a in enumerate([2.0, 3.0, 1e-300, B(0x7FEFFFFFFFFFFFFF), 0.5, 1e10 + 1]):
    dyn.append(cpp_case(f"fsqrt_dDyn{k}", program([("f1", D(a))], "fsqrt.d f3, f1"),
                        f={3: D(math.sqrt(a))}))
for k, a in enumerate([-1.5, 2.5, 0.5, -0.5, 1.5, -2.5, 1e18 + 0.5, 3.7]):
    dyn.append(cpp_case(f"fcvt_l_dDyn{k}", program([("f1", D(a))], "fcvt.l.d x5, f1"),
                        x={5: u64(round(Fraction(a)))}))
for k, v in enumerate([(1 << 63) - 1, (1 << 53) + 1, -((1 << 53) + 1), (1 << 62) + 1, 123456789]):
    dyn.append(cpp_case(f"fcvt_d_lDyn{k}", program([("x6", u64(v))], "fcvt.d.l f3, x6"),
                        f={3: D(float(v))}))

MODE_PAIRS = [(1.0, HALF_ULP_1), (1.0, 3 * HALF_ULP_1), (-1.0, -HALF_ULP_1), (0.1, 0.2), (1.0, TINY),
              (-1.5, TINY), (1.0 + 2 ** -52, 1.0 + 2 ** -52), (3.0, 1.0 / 3.0), (1e10, 1e-10)]
for mode in ("rtz", "rdn", "rup", "rmm"):
    for mn, op in (("fadd.d", lambda a, b: Fraction(a) + Fraction(b)),
                   ("fmul.d", lambda a, b: Fraction(a) * Fraction(b))):
        for k, (a, b) in enumerate(MODE_PAIRS):
            q = op(a, b)
            name = f"{mn.replace('.', '_')}_{mode}{k}"
            text = program([("f1", D(a)), ("f2", D(b))], f"{mn} f3, f1, f2, {mode}")
            if mode == "rmm" and is_tie_double(q):
                ties.append((name, mn, a, b, D(round_double(q, "rmm")), D(float(q))))
                continue
            modes.append(cpp_case(name, text, f={3: D(round_double(q, mode))}))
    for k, a in enumerate([-1.5, 2.5, 0.5, -0.5, 1.25, -1.75, 3.7, -3.7]):
        q = Fraction(a)
        name = f"fcvt_l_d_{mode}{k}"
        text = program([("f1", D(a))], f"fcvt.l.d x5, f1, {mode}")
        if mode == "rmm" and q - math.floor(q) == Fraction(1, 2):
            ties.append((name, "fcvt.l.d", a, None, u64(round_int(q, "rmm")), u64(round(q))))
            continue
        modes.append(cpp_case(name, text, x={5: u64(round_int(q, mode))}))

# ---- 114: the explicit-rm forms of the D conversions, by spec field layout ----
RM = {"rtz": 1, "rdn": 2, "rup": 3, "rmm": 4}
CONV = {  # mnemonic: (funct7, rs2, rd prefix, rs1 prefix)
    "fcvt.w.d": (0b1100001, 0, "x", "f"), "fcvt.wu.d": (0b1100001, 1, "x", "f"),
    "fcvt.l.d": (0b1100001, 2, "x", "f"), "fcvt.lu.d": (0b1100001, 3, "x", "f"),
    "fcvt.d.w": (0b1101001, 0, "f", "x"), "fcvt.d.wu": (0b1101001, 1, "f", "x"),
    "fcvt.s.d": (0b0100000, 1, "f", "f"), "fcvt.d.s": (0b0100001, 0, "f", "f")}
conv = []
for mn, (f7, rs2, rdp, rsp) in CONV.items():
    for rm, code in RM.items():
        rd, rs1 = 5, 6
        word = (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (code << 12) | (rd << 7) | 0b1010011
        conv.append((f"{mn} {rdp}{rd}, {rsp}{rs1}, {rm}", word))

print("// GENERATED by verification/tables/gen_dround.py - do not edit by hand.")
print("const ConvEncoding kConvRm[] = {")
for line, word in conv:
    print(f'    {{"{line}", 0x{word:08X}u}},')
print("};")
print("const IsaCase kDyn[] = {")
print("\n".join(dyn))
print("};")
print("const IsaCase kModes[] = {")
print("\n".join(modes))
print("};")
print(f"{len(dyn)} dyn, {len(modes)} explicit-mode, {len(ties)} rmm ties (not asserted)", file=sys.stderr)
import vpaths  # the RMM ties report is written to verification/out/tables/
with open(vpaths.out("tables", "rmm_ties.txt"), "w") as f:
    for t in ties:
        f.write("\t".join(str(x) if not isinstance(x, int) else hex(x) for x in t) + "\n")
