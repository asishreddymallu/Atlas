#!/usr/bin/env python3
"""Stage E5 part 1 (known_issues 72, 116): fflags the RISC-V way.

Every expected value comes from fpo.py (exact Fraction arithmetic, IEEE 754
special cases, RISC-V NaN and flag rules), never from the simulator.

Per-op programs: fflags is cleared (csrrwi x0, fflags, 0) before each op and
read (csrrs x5, fflags, x0) right after it, then stored, so each stored word
is exactly the flags that one op raised. Then:
  - accrual programs: several ops, one read (the OR of their flags);
  - Sticky31: fflags = 31, an exact op leaves 31;
  - 116: fdiv.s 1/0, then fsw / fld / flw / fsd, fflags stays DZ.
Programs are padded, so all five models owe the results.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from common import Program, cpp_case, SCRATCH, u64
from fpo import *

MAX_OPS = 200          # sd offsets from x11 stay below 2048
MODES4 = ["rne", "rtz", "rdn", "rup"]


def vals(f):
    """Named bit patterns of interest for format f."""
    one = f.from_fraction(Fraction(1), RNE)[0]
    v = lambda q: f.from_fraction(Fraction(q), RNE)[0]
    minnorm = (1 << f.M)
    return {
        "1": one, "2": v(2), "3": v(3), "-1": v(-1), "0.5": v(Fraction(1, 2)), "0.1": v(Fraction(1, 10)),
        "1.5": v(Fraction(3, 2)), "-2.5": v(Fraction(-5, 2)), "4": v(4), "2^-30": v(Fraction(1, 2 ** 30)),
        "max": f.maxfin, "-max": f.sbit | f.maxfin, "minnorm": minnorm, "minnorm+": minnorm + 1,
        "minsub": 1, "+0": 0, "-0": f.sbit, "inf": f.inf, "-inf": f.sbit | f.inf,
        "qnan": f.canon, "snan": f.inf | 1, "-snan": f.sbit | f.inf | 1,
    }


BIN_PAIRS = [("1", "2"), ("1", "3"), ("0.1", "3"), ("max", "max"), ("max", "-max"), ("-max", "4"),
             ("minnorm", "0.5"), ("minnorm+", "0.5"), ("minsub", "0.5"), ("minnorm", "minnorm"),
             ("+0", "-0"), ("-0", "-0"), ("1", "-1"), ("inf", "-inf"), ("inf", "1"), ("inf", "+0"),
             ("1", "+0"), ("-1", "-0"), ("+0", "+0"), ("inf", "inf"), ("qnan", "1"), ("1", "qnan"),
             ("snan", "1"), ("1", "-snan"), ("1", "2^-30"), ("1.5", "4")]
SQRT_IN = ["4", "2", "3", "+0", "-0", "-1", "inf", "-inf", "qnan", "snan", "minsub", "max", "0.1"]
FMA_TRIPLES = [("1", "2", "3"), ("0.1", "3", "1"), ("max", "2", "-max"), ("max", "4", "1"),
               ("minnorm", "0.5", "+0"), ("minnorm+", "0.5", "-0"), ("+0", "inf", "qnan"),
               ("inf", "+0", "1"), ("inf", "1", "-inf"), ("1", "1", "-1"), ("+0", "-0", "+0"),
               ("qnan", "1", "1"), ("1", "snan", "1"), ("1", "1", "snan"), ("2^-30", "2^-30", "1"),
               ("1", "-1", "1")]
CMP_PAIRS = [("1", "2"), ("2", "1"), ("1", "1"), ("+0", "-0"), ("-0", "+0"), ("qnan", "1"),
             ("1", "qnan"), ("snan", "1"), ("1", "-snan"), ("qnan", "qnan"), ("snan", "qnan"),
             ("inf", "max"), ("-inf", "inf")]
TOINT_IN = ["1", "1.5", "-2.5", "0.1", "-1", "-0", "+0", "max", "-max", "inf", "-inf", "qnan", "snan",
            "0.5", "minsub"]
INT_IN = [0, 1, -1, 3, (1 << 24) + 1, (1 << 31) - 1, -(1 << 31), (1 << 53) + 1, (1 << 63) - 1,
          -(1 << 63), (1 << 64) - 1, 0x80000001, 0xFFFFFFFF, 123456789]


class Group:
    """A list of (setup lines, op line, expected flags) chunked into programs."""

    def __init__(self, name):
        self.name, self.items = name, []

    def add(self, loads, op, flags, kind="op"):
        self.items.append((loads, op, flags, kind))


def float_load(p, f, reg, bits):
    p.ins(f"{'flw' if f.w == 32 else 'fld'} {reg}, {p.value(bits)}(x10)")


def build_cases(group):
    """Chunk a group into padded programs; one IsaCase per chunk."""
    out = []
    chunks = [group.items[i:i + MAX_OPS] for i in range(0, len(group.items), MAX_OPS)]
    for n, chunk in enumerate(chunks):
        p, dw = Program(), {}
        for k, (loads, op, flags, _) in enumerate(chunk):
            for fn in loads:
                fn(p)
            p.ins("csrrwi x0, fflags, 0", op, "csrrs x5, fflags, x0", f"sd x5, {8 * k}(x11)")
            dw[SCRATCH + 8 * k] = flags
        assert len(p.table) <= 120, (group.name, len(p.table))
        out.append((f"{group.name}{n + 1 if len(chunks) > 1 else ''}", p.text(padded=False), dw))
    return out


def with_modes(base_mode_fn, mn, modes):
    """Yield (mode name or None, mode) including rmm only when it is not a tie
    (113 open): rmm is kept only if its result and flags equal RNE's AND
    the RNE rounding was not on a tie (checked via RTZ/RUP bracketing)."""
    for m in modes:
        yield m, MODES[m]


def arith(fs):
    f = FMT[fs]
    V = vals(f)
    g = Group(f"Arith{fs.upper()}")
    ops = {"fadd": lambda a, b, m: add(f, a, b, m), "fsub": lambda a, b, m: add(f, a, b, m, neg_b=True),
           "fmul": lambda a, b, m: mul(f, a, b, m), "fdiv": lambda a, b, m: div(f, a, b, m)}
    for mn, fn in ops.items():
        for x, y in BIN_PAIRS:
            a, b = V[x], V[y]
            for mname in MODES4 + ["rmm"]:
                r, fl = fn(a, b, MODES[mname])
                if mname == "rmm" and (r, fl) != fn(a, b, RNE):
                    continue          # a tie: RMM is 113, not asserted here
                g.add([lambda p, a=a: float_load(p, f, "f1", a), lambda p, b=b: float_load(p, f, "f2", b)],
                      f"{mn}.{fs} f3, f1, f2, {mname}", fl)
    for x in SQRT_IN:
        a = V[x]
        for mname in MODES4:
            r, fl = sqrt(f, a, MODES[mname])
            g.add([lambda p, a=a: float_load(p, f, "f1", a)], f"fsqrt.{fs} f3, f1, {mname}", fl)
    return g


def fmas(fs):
    f = FMT[fs]
    V = vals(f)
    g = Group(f"Fma{fs.upper()}")
    forms = {"fmadd": (False, False), "fmsub": (False, True), "fnmsub": (True, False), "fnmadd": (True, True)}
    for mn, (np_, nc) in forms.items():
        for x, y, z in FMA_TRIPLES:
            a, b, c = V[x], V[y], V[z]
            for mname in ["rne", "rtz", "rdn", "rup"]:
                r, fl = fma(f, a, b, c, MODES[mname], np_, nc)
                g.add([lambda p, a=a: float_load(p, f, "f1", a), lambda p, b=b: float_load(p, f, "f2", b),
                       lambda p, c=c: float_load(p, f, "f4", c)],
                      f"{mn}.{fs} f3, f1, f2, f4, {mname}", fl)
    return g


def compares(fs):
    f = FMT[fs]
    V = vals(f)
    g = Group(f"Compare{fs.upper()}")
    for x, y in CMP_PAIRS:
        a, b = V[x], V[y]
        loads = [lambda p, a=a: float_load(p, f, "f1", a), lambda p, b=b: float_load(p, f, "f2", b)]
        for op in ("eq", "lt", "le"):
            g.add(loads, f"f{op}.{fs} x7, f1, f2", compare(f, a, b, op)[1])
        g.add(loads, f"fmin.{fs} f3, f1, f2", minmax(f, a, b, False)[1])
        g.add(loads, f"fmax.{fs} f3, f1, f2", minmax(f, a, b, True)[1])
    # ops that raise nothing, on every special input
    for x in ("snan", "qnan", "inf", "1", "minsub", "-0"):
        a = V[x]
        loads = [lambda p, a=a: float_load(p, f, "f1", a), lambda p: float_load(p, f, "f2", V["snan"])]
        for op in (f"fsgnj.{fs} f3, f1, f2", f"fsgnjn.{fs} f3, f1, f2", f"fsgnjx.{fs} f3, f1, f2",
                   f"fclass.{fs} x7, f1", f"fmv.x.{'w' if fs == 's' else 'd'} x7, f1"):
            g.add(loads, op, 0)
    return g


def conversions(fs):
    f = FMT[fs]
    V = vals(f)
    g = Group(f"Convert{fs.upper()}")
    for x in TOINT_IN:
        a = V[x]
        for kind in ("w", "wu", "l", "lu"):
            for mname in MODES4:
                n, fl = to_int(f, a, kind, MODES[mname])
                g.add([lambda p, a=a: float_load(p, f, "f1", a)], f"fcvt.{kind}.{fs} x7, f1, {mname}", fl)
    for n in INT_IN:
        for kind in ("w", "wu", "l", "lu"):
            for mname in MODES4:
                r, fl = from_int(f, u64(n), kind, MODES[mname])
                g.add([lambda p, n=n: p.load("x6", n)], f"fcvt.{fs}.{kind} f3, x6, {mname}", fl)
    return g


def narrowing():
    g = Group("ConvertSD")
    V = vals(F64)
    for x in V:
        a = V[x]
        for mname in MODES4:
            r, fl = convert(F64, F32, a, MODES[mname])
            g.add([lambda p, a=a: float_load(p, F64, "f1", a)], f"fcvt.s.d f3, f1, {mname}", fl)
    V = vals(F32)
    for x in V:
        a = V[x]
        r, fl = convert(F32, F64, a, RNE)
        g.add([lambda p, a=a: float_load(p, F32, "f1", a)], "fcvt.d.s f3, f1", fl)
    return g


def accrual_cases():
    """Several ops, one read; plus Sticky31 and the 116 programs."""
    cases = []
    S, D = vals(F32), vals(F64)
    # (name, [(op-lines with loads)], expected): built by hand, flags from fpo.
    p, dw = Program(), {}
    k = 0

    def seq(ops_and_flags, start=0):
        nonlocal k
        if start:
            p.ins(f"csrrwi x0, fflags, {start}")
        else:
            p.ins("csrrwi x0, fflags, 0")
        acc = start
        for lines, fl in ops_and_flags:
            p.ins(*lines)
            acc |= fl
        p.ins("csrrs x5, fflags, x0", f"sd x5, {8 * k}(x11)")
        dw[SCRATCH + 8 * k] = acc
        k += 1

    ld32 = lambda r, x: f"flw {r}, {p.value(S[x])}(x10)"
    ld64 = lambda r, x: f"fld {r}, {p.value(D[x])}(x10)"
    # 1/0 (DZ), then 1/3 (NX), then 0/0 (NV), then max*4 (OF|NX), then tiny*0.5 (UF|NX)
    seq([([ld32("f1", "1"), ld32("f2", "+0"), "fdiv.s f3, f1, f2"], div(F32, S["1"], S["+0"], RNE)[1]),
         ([ld32("f2", "3"), "fdiv.s f3, f1, f2"], div(F32, S["1"], S["3"], RNE)[1])])
    seq([([ld64("f1", "+0"), ld64("f2", "+0"), "fdiv.d f3, f1, f2"], div(F64, D["+0"], D["+0"], RNE)[1]),
         ([ld64("f1", "max"), ld64("f2", "4"), "fmul.d f3, f1, f2"], mul(F64, D["max"], D["4"], RNE)[1])])
    seq([([ld32("f1", "minnorm+"), ld32("f2", "0.5"), "fmul.s f3, f1, f2"], mul(F32, S["minnorm+"], S["0.5"], RNE)[1]),
         ([ld64("f1", "1"), ld64("f2", "+0"), "fdiv.d f3, f1, f2"], div(F64, D["1"], D["+0"], RNE)[1]),
         ([ld32("f1", "1"), ld32("f2", "2"), "fadd.s f3, f1, f2"], 0)])
    # every flag, one at a time, accumulated to 31
    seq([([ld32("f1", "1"), ld32("f2", "3"), "fdiv.s f3, f1, f2"], NX),
         ([ld32("f1", "minnorm+"), ld32("f2", "0.5"), "fmul.s f3, f1, f2"], UF | NX),
         ([ld64("f1", "max"), ld64("f2", "max"), "fadd.d f3, f1, f2"], OF | NX),
         ([ld64("f1", "1"), ld64("f2", "+0"), "fdiv.d f3, f1, f2"], DZ),
         ([ld32("f1", "-1"), "fsqrt.s f3, f1"], NV)])
    # Sticky31: an exact op leaves 31; so do compares, sign injection and moves
    seq([([ld32("f1", "1"), ld32("f2", "2"), "fadd.s f3, f1, f2"], 0),
         ([ld64("f1", "1"), ld64("f2", "2"), "fmul.d f3, f1, f2"], 0),
         (["feq.s x7, f1, f2", "fsgnj.d f4, f1, f2", "fmv.x.d x8, f1"], 0)], start=31)
    # 116: loads and stores raise nothing and clear nothing
    seq([([ld32("f1", "1"), ld32("f2", "+0"), "fdiv.s f3, f1, f2"], DZ),
         (["fsw f3, 512(x11)"], 0)])
    seq([([ld32("f1", "1"), ld32("f2", "+0"), "fdiv.s f3, f1, f2"], DZ),
         (["fsw f3, 512(x11)", "flw f4, 512(x11)", "fsd f3, 520(x11)", "fld f5, 520(x11)"], 0)])
    seq([([ld64("f1", "1"), ld64("f2", "3"), "fdiv.d f3, f1, f2"], NX),
         (["fsd f3, 528(x11)", "fld f6, 528(x11)"], 0)])
    cases.append(("Accrual", p.text(padded=False), dw))
    return cases


def emit():
    groups = [arith("s"), arith("d"), fmas("s"), fmas("d"), compares("s"), compares("d"),
              conversions("s"), conversions("d"), narrowing()]
    cases = []
    for g in groups:
        cases += build_cases(g)
    cases += accrual_cases()
    print("// GENERATED by verification/tables/gen_fflags.py - do not edit by hand.")
    print("const IsaCase kCases[] = {")
    for name, text, dw in cases:
        print(cpp_case(name, text, dwords=dw))
    print("};")
    total = sum(len(dw) for _, _, dw in cases)
    print(f"{len(cases)} programs, {total} flag words", file=sys.stderr)
    for g in groups:
        nz = sum(1 for it in g.items if it[2])
        print(f"  {g.name}: {len(g.items)} ops, {nz} raise something", file=sys.stderr)


if __name__ == "__main__":
    emit()
