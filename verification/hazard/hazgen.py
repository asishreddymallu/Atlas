#!/usr/bin/env python3
"""Stage E6 part 2: the unpadded hazard sweep.

  hazgen.py gen      -> out/hazard/batch.s, manifest.json
  hazgen.py run      -> out/hazard/out/<model>.txt (all five models, hermetic)
  hazgen.py compare  -> out/hazard/report.md (+ tsv files)

Each program: a padded setup, then UNPADDED  producer P -> k fillers -> consumer C,
then four nops. P writes register number 5 (x5 or f5) with a known value V; the
setup preloaded x5 and f5 with OTHER values, so a consumer that reads the stale
register gets a different result (a missed stall shows as a value).

Expected values (independent of the simulator):
  - consumer results from E3's RV64IM alu() and E5's fpo.py, plus spec code for
    loads, stores, branches, jalr and the CSR forms;
  - cycles from an ID-time recurrence over the executed stream: t0 = 2, each
    next instruction one cycle later, +2 after a taken jump (resolved in EX),
    +3 after a taken branch (MEM), +4 after a CSR instruction (served at WB;
    a redirect's target is fetched the cycle after the resolving stage); h_nf: a reader of a register written by an earlier
    instruction j waits for t >= t_j + 3 (WB writes, ID reads the same cycle);
    h_f: only a load's reader waits, t >= t_j + 2; cycles = t_last + 3.
"""
import json, os, struct, subprocess, sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "isa"))
import vpaths
OUT = os.path.dirname(vpaths.out("hazard", "x"))  # batch, manifest, out/, report: verification/out/hazard
HARNESS = vpaths.HARNESS
BIN = vpaths.probe("atlas_verify_sweep")
MODELS = ["rvss", "nh-nf", "nh-f", "h-nf", "h-f"]
import fpo
from fpo import F32, F64, RNE
_sw = __import__("sweep")
alu, u64, s64, s32 = _sw.alu, _sw.u64, _sw.s64, _sw.s32

M64 = (1 << 64) - 1
DATA = 0x10000000
SCR = DATA + 0x400
GAP = ["nop"] * 4
A_STORE = SCR + 0x40               # inside the 32 dwords the runner dumps
MARK = [0x1111222233334444, 0x5555666677778888]   # table[0], table[1]: load targets
VX, VX_PRE = 37, 1234              # integer value from P, and the stale preload
FX_PRE = 0x4004000040200000        # f5 preload: single 2.5 in the low half
F_OTHER = 0x3FF400003FA00000       # f6: single 1.25 low, ~1.25 as a double
F_RS3 = 0x4000000040000000         # f8: single 2.0 low
X_OTHER = 3                        # x6
SINGLE_V, DOUBLE_V = 0x3FC00000, 0x3FF8000000000000   # 1.5


# ---- consumers -------------------------------------------------------------
R_INT = "add sub sll slt sltu xor srl sra or and addw subw sllw srlw sraw " \
        "mul mulh mulhsu mulhu div divu rem remu mulw divw divuw remw remuw".split()
I_INT = "addi slti sltiu xori ori andi slli srli srai addiw slliw srliw sraiw".split()
LOADS = {"lb": (1, True), "lh": (2, True), "lw": (4, True), "ld": (8, True),
         "lbu": (1, False), "lhu": (2, False), "lwu": (4, False)}
STORES = {"sb": 1, "sh": 2, "sw": 4, "sd": 8}
BRANCHES = "beq bne blt bge bltu bgeu".split()
FP_RRR = [f"{op}.{w}" for w in "sd" for op in "fadd fsub fmul fdiv fsgnj fsgnjn fsgnjx fmin fmax".split()]
FP_CMP = [f"{op}.{w}" for w in "sd" for op in "feq flt fle".split()]
FP_R4 = [f"{op}.{w}" for w in "sd" for op in "fmadd fmsub fnmsub fnmadd".split()]
F_TO_X = [f"fcvt.{i}.{w}" for w in "sd" for i in ("w", "wu", "l", "lu")] + \
         ["fclass.s", "fclass.d", "fmv.x.w", "fmv.x.d"]
X_TO_F = [f"fcvt.{w}.{i}" for w in "sd" for i in ("w", "wu", "l", "lu")] + ["fmv.w.x", "fmv.d.x"]
F_TO_F1 = ["fsqrt.s", "fsqrt.d", "fcvt.s.d", "fcvt.d.s"]


def consumer_slots():
    """(mnemonic, slot, file, requirement, rd file)."""
    out = []
    for mn in R_INT:
        out += [(mn, "rs1", "x", "any", "x"), (mn, "rs2", "x", "any", "x")]
    for mn in I_INT:
        out.append((mn, "rs1", "x", "any", "x"))
    for mn in LOADS:
        out.append((mn, "rs1", "x", "addr", "x"))
    for mn in STORES:
        out += [(mn, "rs1", "x", "addr", None), (mn, "rs2", "x", "any", None)]
    for mn in BRANCHES:
        out += [(mn, "rs1", "x", "any", None), (mn, "rs2", "x", "any", None)]
    out.append(("jalr", "rs1", "x", "code", "x"))
    for mn in ("csrrw", "csrrs", "csrrc"):
        out.append((mn, "rs1", "x", "any", "x"))
    for mn in FP_RRR:
        out += [(mn, "rs1", "f", "any", "f"), (mn, "rs2", "f", "any", "f")]
    for mn in FP_CMP:
        out += [(mn, "rs1", "f", "any", "x"), (mn, "rs2", "f", "any", "x")]
    for mn in FP_R4:
        out += [(mn, s, "f", "any", "f") for s in ("rs1", "rs2", "rs3")]
    for mn in F_TO_X:
        out.append((mn, "rs1", "f", "any", "x"))
    for mn in X_TO_F:
        out.append((mn, "rs1", "x", "any", "f"))
    for mn in F_TO_F1:
        out.append((mn, "rs1", "f", "any", "f"))
    for mn in ("flw", "fld"):
        out.append((mn, "rs1", "x", "addr", "f"))
    for mn in ("fsw", "fsd"):
        out += [(mn, "rs1", "x", "addr", None), (mn, "rs2", "f", "any", None)]
    return out


# ---- producers -------------------------------------------------------------
# name -> (file, is_load, can(V, req) -> bool, emit(p, rd, V) -> line, default V)
def _int_ok(V, lo, hi):
    return lo <= s64(V) <= hi


def f32_of_int(n):
    b = F32.from_fraction(Fraction(n), RNE)[0]
    return b if F32.value(b) == n else None


PRODUCERS = {}


def producer(name, file, is_load, default, can, emit):
    PRODUCERS[name] = dict(file=file, is_load=is_load, default=default, can=can, emit=emit)


producer("addi", "x", False, VX, lambda V: -2048 <= s64(V) < 2048 or 0 <= V - DATA < 2048,
         lambda p, rd, V: f"addi {rd}, x0, {s64(V)}" if -2048 <= s64(V) < 2048 else f"addi {rd}, x10, {V - DATA}")
producer("add", "x", False, VX, lambda V: True, lambda p, rd, V: (p.setx("x9", V), f"add {rd}, x9, x0")[1])
producer("ld", "x", True, VX, lambda V: True, lambda p, rd, V: f"ld {rd}, {p.value(V)}(x10)")
producer("lw", "x", True, VX, lambda V: s32(V) == s64(V), lambda p, rd, V: f"lw {rd}, {p.value(V)}(x10)")
producer("lbu", "x", True, VX, lambda V: 0 <= V < 256, lambda p, rd, V: f"lbu {rd}, {p.value(V)}(x10)")
producer("mul", "x", False, VX, lambda V: True,
         lambda p, rd, V: (p.setx("x9", V), p.setx("x13", 1), f"mul {rd}, x9, x13")[2])
producer("div", "x", False, VX, lambda V: True,
         lambda p, rd, V: (p.setx("x9", V), p.setx("x13", 1), f"div {rd}, x9, x13")[2])
producer("fcvt.w.s", "x", False, VX, lambda V: s32(V) == s64(V) and f32_of_int(s64(V)) is not None,
         lambda p, rd, V: (p.setf("f9", f32_of_int(s64(V))), f"fcvt.w.s {rd}, f9, rtz")[1])
producer("fmv.x.w", "x", False, VX, lambda V: s32(V) == s64(V),
         lambda p, rd, V: (p.setf("f9", V & 0xFFFFFFFF), f"fmv.x.w {rd}, f9")[1])
producer("fclass.s", "x", False, 64, lambda V: V == 64,
         lambda p, rd, V: (p.setf("f9", 0x3F800000), f"fclass.s {rd}, f9")[1])
producer("feq.s", "x", False, 1, lambda V: V == 1,
         lambda p, rd, V: (p.setf("f9", 0x3F800000), p.setf("f13", 0x3F800000), f"feq.s {rd}, f9, f13")[2])
producer("csrrs", "x", False, 21, lambda V: 0 <= V < 32,
         lambda p, rd, V: (p.setflags(V), f"csrrs {rd}, fflags, x0")[1])
producer("jal", "x", False, None, None, None)      # V = its pc + 4 (layout)
producer("jalr", "x", False, None, None, None)
producer("lui", "x", False, 0x5000, lambda V: (V & 0xFFF) == 0 and s32(V) == s64(V),
         lambda p, rd, V: f"lui {rd}, {(V >> 12) & 0xFFFFF:#x}")
producer("auipc", "x", False, None, None, None)
for _st, _sf in (("sw", "n"), ("sd", "n"), ("fsw", "n"), ("fsd", "n")):
    producer(_st, _sf, False, 0, lambda V: True,
             lambda p, rd, V, _st=_st: (p.setx("x16", A_STORE + 0x20 - 5),
                                        f"{_st} {'x6' if _st in ('sw', 'sd') else 'f6'}, 5(x16)")[1])
producer("fadd.s", "f", False, SINGLE_V, lambda V: True,
         lambda p, rd, V: (p.setf("f9", V), p.setf("f14", 0), f"fadd.s {rd}, f9, f14")[2])
producer("fadd.d", "f", False, DOUBLE_V, lambda V: True,
         lambda p, rd, V: (p.setf("f9", V), p.setf("f14", 0), f"fadd.d {rd}, f9, f14")[2])
producer("flw", "f", True, SINGLE_V, lambda V: V >> 32 == 0, lambda p, rd, V: f"flw {rd}, {p.value(V)}(x10)")
producer("fld", "f", True, DOUBLE_V, lambda V: True, lambda p, rd, V: f"fld {rd}, {p.value(V)}(x10)")
producer("fcvt.s.w", "f", False, 0x40400000, lambda V: V == 0x40400000,
         lambda p, rd, V: (p.setx("x9", 3), f"fcvt.s.w {rd}, x9")[1])
producer("fmv.w.x", "f", False, SINGLE_V, lambda V: V >> 32 == 0,
         lambda p, rd, V: (p.setx("x9", V), f"fmv.w.x {rd}, x9")[1])


class Prog:
    def __init__(self):
        self.table = list(MARK)
        self.setup = ["lui x10, 0x10000", "addi x11, x10, 1024", "csrrwi x0, fflags, 0"]
        self.xinit, self.finit, self.flags = {}, {}, 0

    def value(self, v):
        v = u64(v)
        if v not in self.table:
            self.table.append(v)
        return 8 * self.table.index(v)

    def setx(self, r, v):
        self.xinit[r] = u64(v)

    def setf(self, r, v):
        self.finit[r] = u64(v)

    def setflags(self, v):
        self.flags = v


FILLERS = ["addi x21, x0, 1", "addi x22, x0, 2", "addi x23, x0, 3"]


def build(cons, pname, case, k):
    """Returns (program text, manifest entry) or None when P cannot feed C's slot."""
    mn, slot, sfile, req, rdfile = cons
    P = PRODUCERS[pname]
    p = Prog()
    same = P["file"] == sfile
    # register numbers
    if case == "x0":
        if not (P["file"] == "x" and sfile == "x" and req == "any"):
            return None
        prd, src = "x0", "x0"
    else:
        prd = f"{P['file']}{7 if case == 'other' else 5}"
        src = f"{sfile}5"
    crd = None
    if rdfile:
        crd = f"{rdfile}5" if case == "rdsrc" else f"{rdfile}20"
    if case == "rdsrc" and rdfile != sfile:
        return None
    dep = same and case in ("same", "rdsrc")

    # the value P writes, and the stale preload
    if req == "addr":
        want, pre = DATA, DATA + 8
    elif req == "code":
        want, pre = None, None
    else:
        want = P["default"]
        pre = VX_PRE if sfile == "x" else FX_PRE
    special = pname in ("jal", "jalr", "auipc")
    if not special and want is not None and not P["can"](want):
        if dep:
            return None
        want = P["default"]
    if special and req == "addr" and dep:
        return None

    # layout: setup (padded) then body (unpadded)
    body = []
    labels = {}
    pline = None
    if not special:
        pline = P["emit"](p, prd, want if (want is not None) else P["default"])
    fill = FILLERS[:k]
    # consumer line; operands: varied slot -> src, others -> x6 / f6 / f8
    o1 = src if slot == "rs1" else ("x6" if sfile == "x" or mn in X_TO_F or mn in LOADS else "f6")
    # second operands by class
    if mn in R_INT:
        a = src if slot == "rs1" else "x6"
        b = src if slot == "rs2" else "x6"
        cline = f"{mn} {crd}, {a}, {b}"
    elif mn in I_INT:
        cline = f"{mn} {crd}, {src}, 3"
    elif mn in LOADS or mn in ("flw", "fld"):
        cline = f"{mn} {crd}, OFF({src})"
    elif mn in STORES or mn in ("fsw", "fsd"):
        base = src if slot == "rs1" else "x12"
        data = src if slot == "rs2" else ("x6" if mn in STORES else "f6")
        cline = f"{mn} {data}, OFF({base})"
    elif mn in BRANCHES:
        a = src if slot == "rs1" else "x6"
        b = src if slot == "rs2" else "x6"
        cline = f"{mn} {a}, {b}, bt"
    elif mn == "jalr":
        cline = f"jalr {crd}, OFF({src})"
    elif mn.startswith("csrr"):
        cline = f"{mn} {crd}, fflags, {src}"
    elif mn in FP_RRR or mn in FP_CMP:
        a = src if slot == "rs1" else "f6"
        b = src if slot == "rs2" else "f6"
        cline = f"{mn} {crd}, {a}, {b}"
    elif mn in FP_R4:
        ops = [src if slot == s else d for s, d in (("rs1", "f6"), ("rs2", "f6"), ("rs3", "f8"))]
        cline = f"{mn} {crd}, {', '.join(ops)}"
    elif mn in F_TO_X:
        cline = f"{mn} {crd}, {src}" + (", rtz" if mn.startswith("fcvt") else "")
    elif mn in X_TO_F or mn in F_TO_F1:
        cline = f"{mn} {crd}, {src}"
    else:
        raise KeyError(mn)

    # preloads (x5 and f5 stale values, operands)
    xpre = {"x6": X_OTHER, "x12": A_STORE, "x7": 0}
    fpre = {"f6": F_OTHER, "f8": F_RS3, "f7": 0}
    if req == "addr":
        xpre["x5"] = pre
    elif req == "code":
        pass   # set after layout (x5 = the jalr's target - OFF)
    else:
        xpre["x5"] = VX_PRE
    fpre["f5"] = FX_PRE
    for r, v in p.xinit.items():
        xpre[r] = v
    for r, v in p.finit.items():
        fpre[r] = v

    setup = list(p.setup)
    if p.flags:
        setup.append(f"csrrwi x0, fflags, {p.flags}")
    nsetup_lines = None

    def assemble_lines(xpre, fpre):
        lines = []
        for l in setup:
            lines.append((l, True))
        for r, v in sorted(xpre.items()):
            lines.append((f"ld {r}, {p.value(v)}(x10)", True))
        for r, v in sorted(fpre.items()):
            lines.append((f"fld {r}, {p.value(v)}(x10)", True))
        return lines

    # first pass to know the producer's pc
    def layout(xpre, fpre, pl, cl, off):
        lines = assemble_lines(xpre, fpre)
        pcs, idx = [], 0
        seq = []
        for l, pad in lines:
            seq.append((l, "setup"))
            for g in GAP if pad else []:
                seq.append((g, "pad"))
        seq.append((pl, "P"))
        if special:
            seq.append(("pl:", "label"))
        for f in fill:
            seq.append((f, "fill"))
        seq.append((cl.replace("OFF", str(off)), "C"))
        if mn in BRANCHES:
            seq.append(("addi x24, x0, 1", "skip"))
            seq.append(("bt:", "label"))
        if mn == "jalr":
            seq.append(("jt:", "label"))
        for g in GAP:
            seq.append((g, "tail"))
        pc, addr = 0, {}
        for l, role in seq:
            if role == "label":
                addr[l[:-1]] = pc
            else:
                addr.setdefault("@" + role, pc)
                pc += 4
        return seq, addr

    off = 0
    if req == "addr":
        # the base register holds DATA (P's value) or DATA + 8 (the preload)
        off = (A_STORE - DATA) if (mn in STORES or mn in ("fsw", "fsd")) else 0
    if special:
        pl0 = {"jal": f"jal {prd}, pl", "jalr": f"jalr {prd}, 0(x15)", "auipc": f"auipc {prd}, 0"}[pname]
    else:
        pl0 = pline
    # iterate layout until addresses settle (preload table offsets do not move pcs)
    xpre_try = dict(xpre)
    if pname == "jalr":
        xpre_try["x15"] = 0
    if req == "code":
        xpre_try["x5"] = 0
    seq, addr = layout(xpre_try, fpre, pl0, cline, 0)
    ppc = addr["@P"]
    if special:
        V = ppc + 4 if pname in ("jal", "jalr") else ppc
    else:
        V = want if want is not None else P["default"]
    if pname == "jalr":
        xpre_try["x15"] = addr["pl"]
    jt = addr.get("jt")
    if req == "code":
        # jalr's target is the label after it; the value the slot sees is V (if a real
        # dependency) or the preload; both are made equal so either lands on jt
        if not dep and same and case == "other":
            pass
        sv = V if dep else 40          # the value the jalr's base register holds
        off = jt - sv
        if not (-2048 <= off < 2048):
            return None
        xpre_try["x5"] = sv
    if req == "addr" and dep and special:
        return None
    seq, addr = layout(xpre_try, fpre, pl0, cline, off)
    assert addr["@P"] == ppc
    text = [".data", "vals: .dword " + ", ".join(hex(t) for t in p.table), ".text"]
    for l, role in seq:
        text.append(l if role == "label" else "    " + l)
    info = dict(cons=list(cons), producer=pname, case=case, k=k, dep=dep, V=u64(V),
                prd=prd, src=src, crd=crd, off=off, xpre=xpre_try, fpre=fpre, flags=p.flags,
                table=p.table, seq=[[l, r] for l, r in seq])
    return "\n".join(text) + "\n", info


# ---- analytic cycles ---------------------------------------------------------
def reg_rw(line):
    """Spec reads/writes of one instruction line (registers as 'x5', 'f5')."""
    mn, _, rest = line.partition(" ")
    ops = [o.strip() for o in rest.replace("(", ",").replace(")", "").split(",") if o.strip()]
    regs = [o for o in ops if o[0] in "xf" and o[1:].isdigit()]
    if mn in ("nop",) or line.endswith(":"):
        return set(), set()
    if mn in ("lui", "auipc", "jal"):
        return {regs[0]}, set()
    if mn.startswith("csrr"):
        w = {regs[0]}
        return w, ({regs[1]} if not mn.endswith("i") and len(regs) > 1 else set())
    if mn in STORES or mn in ("fsw", "fsd") or mn in BRANCHES:
        return set(), set(regs)
    return {regs[0]}, set(regs[1:])


def cycles(seq, model):
    """seq: the executed stream [(line, role)]; jumps/branches taken as marked."""
    t, info = [], []
    prev_penalty = 0
    for line, role in seq:
        if role == "label":
            continue
        w, r = reg_rw(line)
        w = {x for x in w if x != "x0"}
        r = {x for x in r if x != "x0"}
        mn = line.split()[0]
        ti = 2 if not t else t[-1] + 1 + prev_penalty
        for (pw, pload), tj in zip(info, t):
            if pw & r:
                if model == "h-nf":
                    ti = max(ti, tj + 3)
                elif model == "h-f" and pload:
                    ti = max(ti, tj + 2)
        t.append(ti)
        info.append((w, mn in LOADS or mn in ("flw", "fld")))
        prev_penalty = 0
        if mn in ("jal", "jalr"):
            prev_penalty = 2
        elif mn.startswith("csrr"):
            prev_penalty = 4    # served at WB; the target is fetched the cycle after (rv5s_processor_base.cpp:493)
        elif mn in BRANCHES and role == "C!taken":
            prev_penalty = 3
    return t[-1] + 3


def executed(info, taken):
    out = []
    for l, role in info["seq"]:
        if role == "skip" and taken:
            continue
        if role == "C" and taken:
            role = "C!taken"
        out.append((l, role))
    return out


# ---- value oracle ------------------------------------------------------------
def fclass(bits, w):
    """RISC-V fclass mask (spec table), as in verification/tables/gen_fclass.py."""
    eb, fbits = (8, 23) if w == 32 else (11, 52)
    sign = bits >> (w - 1) & 1
    exp = bits >> fbits & ((1 << eb) - 1)
    frac = bits & ((1 << fbits) - 1)
    if exp == (1 << eb) - 1:
        return 1 << (0 if sign else 7) if frac == 0 else 1 << (9 if frac >> (fbits - 1) else 8)
    if exp == 0:
        return 1 << (3 if sign else 4) if frac == 0 else 1 << (2 if sign else 5)
    return 1 << (1 if sign else 6)


def fb(fmt, v):
    return v & 0xFFFFFFFF if fmt == "s" else v


def oracle(info):
    """Expected consumer effect: dict of 'xN'/'fN'/'mN' -> value, and whether a branch is taken."""
    mn, slot, sfile, req, rdfile = info["cons"]
    xpre, fpre = dict(info["xpre"]), dict(info["fpre"])
    regs = {}
    for r, v in xpre.items():
        regs[r] = v
    for r, v in fpre.items():
        regs[r] = v
    regs["x0"] = 0
    if info["prd"] != "x0":
        regs[info["prd"]] = info["V"]
    get = lambda r: regs.get(r, 0)
    src = info["src"]
    val = get(src)
    crd = info["crd"]
    o1 = lambda d: val if slot == "rs1" else get(d)
    o2 = lambda d: val if slot == "rs2" else get(d)
    o3 = lambda d: val if slot == "rs3" else get(d)
    res = {}
    taken = False
    if mn in R_INT:
        res[crd] = alu(mn, o1("x6"), o2("x6"))
    elif mn in I_INT:
        res[crd] = alu(mn, val, 3)
    elif mn in LOADS:
        size, sign = LOADS[mn]
        addr = u64(val + info["off"])
        word = info["table"][(addr - DATA) // 8] if DATA <= addr < DATA + 8 * len(info["table"]) else None
        if word is None:
            return None, None
        v = (word >> (8 * (addr % 8))) & ((1 << (8 * size)) - 1)
        if sign and v >> (8 * size - 1):
            v -= 1 << (8 * size)
        res[crd] = u64(v)
    elif mn in ("flw", "fld"):
        addr = u64(val + info["off"])
        word = info["table"][(addr - DATA) // 8]
        res[crd] = word & 0xFFFFFFFF if mn == "flw" else word
    elif mn in STORES or mn in ("fsw", "fsd"):
        base = val if slot == "rs1" else get("x12")
        data = val if slot == "rs2" else get("x6" if mn in STORES else "f6")
        size = STORES.get(mn, 4 if mn == "fsw" else 8)
        addr = u64(base + info["off"])
        if not (SCR <= addr < SCR + 256) or addr % 8:
            return None, None
        res["m%d" % ((addr - SCR) // 8)] = data & ((1 << (8 * size)) - 1)
    elif mn in BRANCHES:
        a, b = o1("x6"), o2("x6")
        taken = {"beq": a == b, "bne": a != b, "blt": s64(a) < s64(b), "bge": s64(a) >= s64(b),
                 "bltu": a < b, "bgeu": a >= b}[mn]
        res["x24"] = 0 if taken else 1
    elif mn == "jalr":
        pc = None
        a = 0
        for l, role in info["seq"]:
            if role == "label":
                continue
            if role == "C":
                pc = a
            a += 4
        res[crd] = pc + 4
    elif mn.startswith("csrr"):
        old = info["flags"] & 31
        res[crd] = old
    else:
        fmt = mn.split(".")[-1] if mn.split(".")[0] != "fcvt" else None
        F = F32 if mn.endswith(".s") else F64
        if mn in FP_RRR:
            op = mn.split(".")[0]
            a, b = fb(mn[-1], o1("f6")), fb(mn[-1], o2("f6"))
            if op in ("fadd", "fsub"):
                r = fpo.add(F, a, b, RNE, neg_b=op == "fsub")[0]
            elif op == "fmul":
                r = fpo.mul(F, a, b, RNE)[0]
            elif op == "fdiv":
                r = fpo.div(F, a, b, RNE)[0]
            elif op in ("fmin", "fmax"):
                r = fpo.minmax(F, a, b, op == "fmax")[0]
            else:
                sb = F.sbit
                s = {"fsgnj": b & sb, "fsgnjn": (~b) & sb, "fsgnjx": (a ^ b) & sb}[op]
                r = (a & ~sb) | s
            res[crd] = r
        elif mn in FP_CMP:
            a, b = fb(mn[-1], o1("f6")), fb(mn[-1], o2("f6"))
            res[crd] = fpo.compare(F, a, b, {"feq": "eq", "flt": "lt", "fle": "le"}[mn.split(".")[0]])[0]
        elif mn in FP_R4:
            a, b, c = fb(mn[-1], o1("f6")), fb(mn[-1], o2("f6")), fb(mn[-1], o3("f8"))
            np_, nc = {"fmadd": (False, False), "fmsub": (False, True), "fnmsub": (True, False),
                       "fnmadd": (True, True)}[mn.split(".")[0]]
            res[crd] = fpo.fma(F, a, b, c, RNE, np_, nc)[0]
        elif mn in F_TO_X:
            w = "s" if mn in ("fmv.x.w", "fclass.s") or mn.endswith(".s") else "d"
            a = fb(w, val)
            Fw = F32 if w == "s" else F64
            if mn.startswith("fcvt"):
                kind = mn.split(".")[1]
                res[crd] = fpo.to_int(Fw, a, kind, fpo.RTZ)[0]
            elif mn.startswith("fclass"):
                res[crd] = fclass(a, 32 if w == "s" else 64)
            else:
                res[crd] = u64(s32(a)) if w == "s" else a
        elif mn in X_TO_F:
            if mn.startswith("fmv"):
                res[crd] = val & 0xFFFFFFFF if mn == "fmv.w.x" else val
            else:
                _, d, kind = mn.split(".")
                res[crd] = fpo.from_int(F32 if d == "s" else F64, val, kind, RNE)[0]
        elif mn in F_TO_F1:
            if mn.startswith("fsqrt"):
                res[crd] = fpo.sqrt(F, fb(mn[-1], val), RNE)[0]
            elif mn == "fcvt.s.d":
                res[crd] = fpo.convert(F64, F32, val, RNE)[0]
            else:
                res[crd] = fpo.convert(F32, F64, val & 0xFFFFFFFF, RNE)[0]
    return res, taken


# ---- gen / run / compare -------------------------------------------------------
CASES = ["same", "other", "x0", "rdsrc"]


def gen():
    progs, manifest = [], {}
    skipped = Counter()
    for cons in consumer_slots():
        for pname in PRODUCERS:
            for case in CASES:
                for k in range(4):
                    r = build(cons, pname, case, k)
                    if r is None:
                        skipped[(case)] += 1
                        continue
                    text, info = r
                    pid = f"p{len(progs)}"
                    progs.append((pid, text))
                    manifest[pid] = info
    with open(os.path.join(OUT, "batch.s"), "w") as f:
        for pid, text in progs:
            f.write(f"### {pid}\n{text}")
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f)
    print(f"{len(progs)} programs; skipped (no producer for that slot/case): {dict(skipped)}")


def run():
    os.makedirs(os.path.join(OUT, "out"), exist_ok=True)

    def one(m):
        out = os.path.join(OUT, "out", m + ".txt")
        subprocess.run([HARNESS, BIN, "run", m, os.path.join(OUT, "batch.s"), out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return m, sum(1 for _ in open(out))
    with ThreadPoolExecutor(5) as ex:
        for m, n in ex.map(one, MODELS):
            print(m, n, "results")


def parse(path):
    res = {}
    for line in open(path):
        parts = line.split()
        pid = parts[0]
        if parts[1] != "OK":
            res[pid] = {"err": " ".join(parts[2:])}
            continue
        kv = dict(p.split("=") for p in parts[2:5])
        vals = [int(v, 16) for v in parts[5:]]
        res[pid] = {"cycles": int(kv["cycles"]), "x": vals[0:32], "f": vals[32:64], "fcsr": vals[64],
                    "mem": vals[65:97]}
    return res


def state(r):
    return (tuple(r["x"]), tuple(r["f"]), r["fcsr"], tuple(r["mem"]))


def compare():
    manifest = json.load(open(os.path.join(OUT, "manifest.json")))
    R = {m: parse(os.path.join(OUT, "out", m + ".txt")) for m in MODELS}
    value_div = defaultdict(list)       # (model, class) -> [pid]
    cyc_div = defaultdict(list)
    orc_mis = defaultdict(list)
    errs = Counter()
    nhnf_min = defaultdict(lambda: defaultdict(lambda: 99))
    nhf_viol = defaultdict(list)
    no_oracle = Counter()
    total = 0
    for pid, info in manifest.items():
        total += 1
        ref = R["rvss"].get(pid)
        if ref is None or "err" in ref:
            errs[(info["cons"][0], (ref or {}).get("err", "missing")[:60])] += 1
            continue
        mn, slot, sfile, req, rdfile = info["cons"]
        pclass = info["producer"]
        relation = ("dep" if info["dep"] else
                    "cross-file" if info["case"] in ("same", "rdsrc") else info["case"])
        key = f"{pclass} -> {mn}.{slot} [{relation}]"
        # oracle on rvss
        exp, taken = oracle(info)
        if exp is None:
            no_oracle[mn] += 1
        else:
            for r, v in exp.items():
                if r[0] == "m":
                    got = ref["mem"][int(r[1:])]
                    v = v if v is not None else 0
                    mask = (1 << 64) - 1
                    if mn in STORES or mn in ("fsw", "fsd"):
                        size = STORES.get(mn, 4 if mn == "fsw" else 8)
                        mask = (1 << (8 * size)) - 1
                    ok = (got & mask) == v
                else:
                    got = ref["x" if r[0] == "x" else "f"][int(r[1:])]
                    ok = got == u64(v) or r in ("x0",)
                if not ok:
                    orc_mis[f"{mn}.{slot}"].append((pid, r, hex(got), hex(u64(v))))
        seq = executed(info, taken)
        for m in ("h-nf", "h-f"):
            got = R[m].get(pid)
            if got is None or "err" in got:
                errs[(m, mn)] += 1
                continue
            if state(got) != state(ref):
                value_div[(m, key)].append((pid, info["k"]))
            want = cycles(seq, m)
            if got["cycles"] != want:
                cyc_div[(m, key, info["k"], got["cycles"] - want)].append(pid)
        # nh models: contracts
        dep_load = info["dep"] and PRODUCERS[pclass]["is_load"]
        g = R["nh-f"].get(pid)
        if g and "err" not in g and state(g) != state(ref):
            if not dep_load or info["k"] >= 1:
                nhf_viol[key].append((pid, info["k"]))
        g = R["nh-nf"].get(pid)
        if info["dep"] and g and "err" not in g:
            if state(g) == state(ref):
                pass
            nhnf_min_key = (pclass, f"{mn}.{slot}")
            nhnf_min[nhnf_min_key]["ok" + str(info["k"])] = state(g) == state(ref)
    # minimal padding for nh-nf per producer class: smallest k such that every consumer
    # equals rvss at k and every larger k
    mins = defaultdict(lambda: Counter())
    k3_fail = []
    for (pclass, cs), d in nhnf_min.items():
        ks = [k for k in range(4) if all(d.get("ok" + str(j), True) for j in range(k, 4))]
        mins[pclass][min(ks) if ks else "none"] += 1
        if not d.get("ok3", True):
            k3_fail.append((pclass, cs))
    with open(os.path.join(OUT, "report.md"), "w") as f:
        f.write(f"programs {total}, assemble/run errors {sum(errs.values())}\n\n")
        f.write(f"## h-nf/h-f final state != rvss: {sum(len(v) for v in value_div.values())} programs in {len(value_div)} classes\n")
        for (m, key), v in sorted(value_div.items()):
            f.write(f"- {m} {key}: k={sorted(set(k for _, k in v))} e.g. {v[0][0]}\n")
        f.write(f"\n## cycles != analytic: {sum(len(v) for v in cyc_div.values())} programs in {len(cyc_div)} classes\n")
        for (m, key, k, d), v in sorted(cyc_div.items()):
            f.write(f"- {m} {key} k={k}: got-want={d:+d} x{len(v)} e.g. {v[0]}\n")
        f.write(f"\n## rvss vs oracle: {sum(len(v) for v in orc_mis.values())} in {len(orc_mis)} consumer slots\n")
        for c, v in sorted(orc_mis.items()):
            f.write(f"- {c}: {len(v)} e.g. {v[0]}\n")
        f.write(f"\nno oracle: {dict(no_oracle)}\n")
        f.write(f"\n## nh-f contract violations: {sum(len(v) for v in nhf_viol.values())} in {len(nhf_viol)} classes\n")
        for key, v in sorted(nhf_viol.items()):
            f.write(f"- {key}: k={sorted(set(k for _, k in v))} e.g. {v[0][0]}\n")
        f.write("\n## nh-nf minimal fillers (per producer class: count of consumer slots by minimal k)\n")
        for pclass in PRODUCERS:
            f.write(f"- {pclass}: {dict(mins[pclass])}\n")
        f.write(f"\nnh-nf fails at k=3: {k3_fail[:20]} ({len(k3_fail)})\n")
        f.write(f"\nerrors: {errs.most_common(20)}\n")
    print(open(os.path.join(OUT, "report.md")).read()[:6000])


if __name__ == "__main__":
    {"gen": gen, "run": run, "compare": compare}[sys.argv[1]]()
