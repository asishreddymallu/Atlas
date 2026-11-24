#!/usr/bin/env python3
"""The all-instruction differential sweep (Stage E2).

  sweep.py lists    the probe's `list` and `names` -> list.txt, names.txt
  sweep.py gen      list.txt + names.txt -> batch.s, manifest.json
  sweep.py run      batch.s on all five models -> out/<model>.txt
  sweep.py compare  -> divergences.tsv, oracle.tsv, table.txt

The mnemonic list is the union of instruction_syntax_map (the probe's `list`)
and instruction_string_map (the probe's `names`) - the assembler's own tables.
Operand values come from a .data table loaded with ld/fld, and every
instruction is followed by four nops, so all five models are inside their
contract. The integer oracle below is independent RV64IM in Python.
"""
import json, os, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

SRC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(SRC, "..", "lib"))
import vpaths
from probeio import parse_line
TABLES = os.path.join(vpaths.VERIFY, "tables")
HERE = os.path.dirname(vpaths.out("isa", "x"))  # generated files: verification/out/isa
HARNESS = vpaths.HARNESS
BIN = vpaths.probe("atlas_verify_sweep")
MODELS = ["rvss", "nh-nf", "nh-f", "h-nf", "h-f"]
M64 = (1 << 64) - 1
DATA, SCRATCH_OFF = 0x10000000, 0x400
GAP = ["nop"] * 4

def u64(v): return v & M64
def s64(v): v &= M64; return v - (1 << 64) if v >> 63 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v >> 31 else v
def sx(v, bits): v &= (1 << bits) - 1; return v - (1 << bits) if v >> (bits - 1) else v

V = [0, 1, u64(-1), 1 << 63, (1 << 63) - 1, u64(-(1 << 31)), (1 << 31) - 1, 1 << 31]
VNAME = ["0", "1", "-1", "INT64_MIN", "INT64_MAX", "INT32_MIN", "INT32_MAX", "0x80000000"]
SHAMT = [0, 31, 32, 63]
IMM = [0, 1, -1, 2047, -2048]
UIMM = [0, 1, 0x7FFFF, 0x80000, 0xFFFFF]
D = lambda f: struct.unpack("<Q", struct.pack("<d", f))[0]
S = lambda f: struct.unpack("<I", struct.pack("<f", f))[0]
FD = [("+0", 0), ("-0", 1 << 63), ("1", D(1.0)), ("-1.5", D(-1.5)), ("inf", D(float("inf"))),
      ("-inf", D(float("-inf"))), ("qNaN", 0x7FF8000000000000), ("sNaN", 0x7FF4000000000000),
      ("max", 0x7FEFFFFFFFFFFFFF), ("minnorm", 0x0010000000000000), ("denorm", 1)]
FS = [("+0", 0), ("-0", 1 << 31), ("1", S(1.0)), ("-1.5", S(-1.5)), ("inf", 0x7F800000),
      ("-inf", 0xFF800000), ("qNaN", 0x7FC00000), ("sNaN", 0x7FA00000), ("max", 0x7F7FFFFF),
      ("minnorm", 0x00800000), ("denorm", 1)]
FSMALL = [0, 2, 4, 6]                      # +0, 1, inf, qNaN: for the 3-operand FMA
FPAIR = [0, 1, 2, 3, 4, 6, 7, 10]         # the binary-op subset

INT_R = ("add sub and or xor sll srl sra slt sltu mul mulh mulhsu mulhu div divu rem remu "
         "addw subw sllw srlw sraw mulw divw divuw remw remuw").split()
INT_I = "addi andi ori xori slti sltiu addiw".split()
INT_SH = "slli srli srai slliw srliw sraiw".split()
LOADS = {"lb": (1, True), "lh": (2, True), "lw": (4, True), "ld": (8, False),
         "lbu": (1, False), "lhu": (2, False), "lwu": (4, False)}
STORES = {"sb": 1, "sh": 2, "sw": 4, "sd": 8}
UNARY_PSEUDO = "mv not neg negw sext.w seqz snez sltz sgtz".split()
LOAD_PATTERN = 0x8081828384858687
EXCLUDED = {"branches and jumps (Part 1)": "beq bne blt bge bltu bgeu jal jalr j jr ret call tail "
            "beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu".split(),
            "system/fence (Part 4, 52)": "ecall ebreak fence fence_i".split(),
            "pc/label-relative (Part 1)": "auipc la".split(),
            "no operands": ["nop"]}

class Gen:
    def __init__(self):
        self.progs, self.manifest = [], {}
    def add(self, mn, ops, table, body, oracle=None, kind="int"):
        pid = f"p{len(self.progs)}"
        lines = [".data", "vals: .dword " + ", ".join(hex(t) for t in table), ".text",
                 "    lui x10, 0x10000", *["    " + g for g in GAP],
                 "    addi x11, x10, %d" % SCRATCH_OFF, *["    " + g for g in GAP]]
        for b in body:
            lines.append("    " + b)
            lines += ["    " + g for g in GAP]
        self.progs.append((pid, "\n".join(lines) + "\n"))
        self.manifest[pid] = {"mn": mn, "ops": ops, "oracle": oracle, "kind": kind}

# ---- the Python RV64IM oracle ----------------------------------------------
def alu(mn, a, b):
    a, b = u64(a), u64(b)
    sa, sb = s64(a), s64(b)
    w = mn.endswith("w") and mn not in ("mulhsu",)
    if mn in ("add", "addi"): return u64(a + b)
    if mn == "sub": return u64(a - b)
    if mn in ("and", "andi"): return a & b
    if mn in ("or", "ori"): return a | b
    if mn in ("xor", "xori"): return a ^ b
    if mn in ("sll", "slli"): return u64(a << (b & 63))
    if mn in ("srl", "srli"): return a >> (b & 63)
    if mn in ("sra", "srai"): return u64(sa >> (b & 63))
    if mn in ("slt", "slti"): return int(sa < sb)
    if mn in ("sltu", "sltiu"): return int(a < b)
    if mn == "mul": return u64(a * b)
    if mn == "mulh": return u64((sa * sb) >> 64)
    if mn == "mulhsu": return u64((sa * b) >> 64)
    if mn == "mulhu": return u64((a * b) >> 64)
    def div(x, y):  # RISC-V: round toward zero, /0 -> -1, overflow -> x
        if y == 0: return -1
        q = abs(x) // abs(y)
        return q if (x < 0) == (y < 0) else -q
    def rem(x, y):
        if y == 0: return x
        return x - div(x, y) * y
    if mn == "div": return a if (sa == -(1 << 63) and sb == -1) else u64(div(sa, sb))
    if mn == "divu": return M64 if b == 0 else a // b
    if mn == "rem": return 0 if (sa == -(1 << 63) and sb == -1) else u64(rem(sa, sb))
    if mn == "remu": return a if b == 0 else a % b
    a32, b32 = a & 0xFFFFFFFF, b & 0xFFFFFFFF
    sa32, sb32 = s32(a), s32(b)
    if mn in ("addw", "addiw"): return u64(s32(a + b))
    if mn == "subw": return u64(s32(a - b))
    if mn in ("sllw", "slliw"): return u64(s32(a32 << (b & 31)))
    if mn in ("srlw", "srliw"): return u64(s32(a32 >> (b & 31)))
    if mn in ("sraw", "sraiw"): return u64(s32(sa32 >> (b & 31)))
    if mn == "mulw": return u64(s32(a * b))
    if mn == "divw": return u64(s32(a32 if (sa32 == -(1 << 31) and sb32 == -1) else div(sa32, sb32)))
    if mn == "divuw": return u64(s32(0xFFFFFFFF if b32 == 0 else a32 // b32))
    if mn == "remw": return u64(s32(0 if (sa32 == -(1 << 31) and sb32 == -1) else rem(sa32, sb32)))
    if mn == "remuw": return u64(s32(a32 if b32 == 0 else a32 % b32))
    raise KeyError(mn)

def pseudo(mn, a):
    a = u64(a)
    return {"mv": a, "not": a ^ M64, "neg": u64(-a), "negw": u64(s32(-a)), "sext.w": u64(s32(a)),
            "seqz": int(a == 0), "snez": int(a != 0), "sltz": int(s64(a) < 0),
            "sgtz": int(s64(a) > 0)}[mn]

# ---- generation -------------------------------------------------------------
def lists():
    """The assembler's own mnemonic tables, read through the probe."""
    for cmd, name in (("list", "list.txt"), ("names", "names.txt")):
        with open(os.path.join(HERE, name), "w") as f:
            subprocess.run([HARNESS, BIN, cmd], stdout=f, stderr=subprocess.DEVNULL, check=True)
        # run_hermetic.sh appends its own "--- hermetic" report; keep the probe's lines only
        lines = [l for l in open(os.path.join(HERE, name)) if l.strip() and not l.startswith(("---", " "))]
        open(os.path.join(HERE, name), "w").writelines(lines)
        print(name, len(lines), "lines")


def gen():
    syntax = {}
    for line in open(os.path.join(HERE, "list.txt")):
        p = line.split()
        syntax[p[0]] = [int(x) for x in p[1:]]
    names = {l.strip() for l in open(os.path.join(HERE, "names.txt")) if l.strip()}
    allm = sorted(set(syntax) | names)
    g = Gen()
    covered = set()
    for mn in allm:
        if mn in INT_R:
            covered.add(mn)
            amts = SHAMT if mn in ("sll", "srl", "sra", "sllw", "srlw", "sraw") else None
            for i, a in enumerate(V):
                for j, b in enumerate(amts if amts else V):
                    g.add(mn, f"x5, x6={VNAME[i]}, x7={b if amts else VNAME[j]}", [a, b],
                          ["ld x6, 0(x10)", "ld x7, 8(x10)", f"{mn} x5, x6, x7"],
                          {"x5": alu(mn, a, b)})
            a, b = V[2], V[6]
            for shape, ins, reg, exp in [
                    ("rd=x0", f"{mn} x0, x6, x7", "x0", 0),
                    ("rs1=rd", f"{mn} x6, x6, x7", "x6", alu(mn, a, b)),
                    ("rs1=rs2", f"{mn} x5, x6, x6", "x5", alu(mn, a, a)),
                    ("rd=x31", f"{mn} x31, x6, x7", "x31", alu(mn, a, b))]:
                g.add(mn, f"{shape} (-1, INT32_MAX)", [a, b], ["ld x6, 0(x10)", "ld x7, 8(x10)", ins],
                      {reg: exp})
        elif mn in INT_I:
            covered.add(mn)
            for i, a in enumerate(V):
                for imm in IMM:
                    g.add(mn, f"x5, x6={VNAME[i]}, {imm}", [a], ["ld x6, 0(x10)", f"{mn} x5, x6, {imm}"],
                          {"x5": alu(mn, a, u64(imm))})
            g.add(mn, "rd=x0", [V[2]], ["ld x6, 0(x10)", f"{mn} x0, x6, 1"], {"x0": 0})
            g.add(mn, "rs1=rd", [V[2]], ["ld x6, 0(x10)", f"{mn} x6, x6, 1"], {"x6": alu(mn, V[2], 1)})
            g.add(mn, "rd=x31", [V[2]], ["ld x6, 0(x10)", f"{mn} x31, x6, 1"], {"x31": alu(mn, V[2], 1)})
        elif mn in INT_SH:
            covered.add(mn)
            for i, a in enumerate(V):
                for sh in SHAMT:
                    g.add(mn, f"x5, x6={VNAME[i]}, {sh}", [a], ["ld x6, 0(x10)", f"{mn} x5, x6, {sh}"],
                          {"x5": alu(mn, a, sh)})
            g.add(mn, "rd=x0", [V[2]], ["ld x6, 0(x10)", f"{mn} x0, x6, 1"], {"x0": 0})
            g.add(mn, "rs1=rd", [V[2]], ["ld x6, 0(x10)", f"{mn} x6, x6, 1"], {"x6": alu(mn, V[2], 1)})
        elif mn == "lui":
            covered.add(mn)
            for imm in UIMM:
                g.add(mn, f"x5, {hex(imm)}", [0], [f"lui x5, {hex(imm)}"], {"x5": u64(s32(imm << 12))})
            g.add(mn, "rd=x31", [0], ["lui x31, 1"], {"x31": 0x1000})
        elif mn == "li":
            covered.add(mn)
            for i, a in enumerate(V):  # li's chain cannot be padded: nh-nf by design (77)
                g.add(mn, f"x5, {VNAME[i]}", [0], [f"li x5, {s64(a)}"], {"x5": a}, kind="li")
            # E3: the 110 range and the boundaries around it
            for a in (0x7FFFF7FF, 0x7FFFF800, 0x7FFFFABC, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000,
                      u64(-0x7FFFF800), u64(-0x7FFFFFFF), u64(-0x80000000), 0xFFFFFFFF, 1 << 32,
                      0x7FFFFFFF7FFFFFFF, 0x000000017FFFF800):
                g.add(mn, f"x5, {hex(a)}", [0], [f"li x5, {s64(a)}"], {"x5": a}, kind="li")
        elif mn in LOADS:
            covered.add(mn)
            size, signed = LOADS[mn]
            pat = [LOAD_PATTERN, 0x7F6E5D4C3B2A1908]
            raw = struct.pack("<QQ", *pat)
            for off in range(0, 8):
                val = int.from_bytes(raw[off:off + size], "little")
                exp = u64(sx(val, size * 8)) if signed else val
                g.add(mn, f"x5, {off}(x10)", pat, [f"{mn} x5, {off}(x10)"], {"x5": exp})
            g.add(mn, "rd=x0", pat, [f"{mn} x0, 0(x10)"], {"x0": 0})
            g.add(mn, "rs1=rd", pat, ["addi x6, x10, 0", f"{mn} x6, 0(x6)"],
                  {"x6": int.from_bytes(raw[0:size], "little") if not signed else u64(sx(int.from_bytes(raw[0:size], "little"), size * 8))})
        elif mn in STORES:
            covered.add(mn)
            size = STORES[mn]
            for i, a in enumerate(V):
                for off in (0, 1, 3, 4, 7):
                    mem = bytearray(32 * 8)
                    mem[off:off + size] = (a & ((1 << (8 * size)) - 1)).to_bytes(size, "little")
                    exp = {f"m{k}": int.from_bytes(mem[8 * k:8 * k + 8], "little") for k in range(2)}
                    g.add(mn, f"x6={VNAME[i]}, {off}(x11)", [a], ["ld x6, 0(x10)", f"{mn} x6, {off}(x11)"], exp)
        elif mn in UNARY_PSEUDO:
            covered.add(mn)
            for i, a in enumerate(V):
                g.add(mn, f"x5, x6={VNAME[i]}", [a], ["ld x6, 0(x10)", f"{mn} x5, x6"], {"x5": pseudo(mn, a)})
        else:
            s = syntax.get(mn, [])
            dbl = mn.endswith(".d") or mn in ("fld", "fsd") or ".d." in mn
            vals = FD if dbl else FS
            fl = "fld" if dbl else "flw"
            if mn in ("fmv.s.x", "fmv.x.s"):
                s = [18] if mn == "fmv.s.x" else [20]
            if not s:
                continue
            c = s[0]
            def fload(reg, k):
                return f"{fl} {reg}, {8 * k}(x10)"
            if c == 14:      # binary FP
                covered.add(mn)
                for i in FPAIR:
                    for j in FPAIR:
                        g.add(mn, f"f3, {vals[i][0]}, {vals[j][0]}", [vals[i][1], vals[j][1]],
                              [fload("f1", 0), fload("f2", 1), f"{mn} f3, f1, f2"], kind="fp")
                g.add(mn, "rs1=rs2=rd", [vals[2][1]], [fload("f1", 0), f"{mn} f1, f1, f1"], kind="fp")
            elif c == 12:    # fused
                covered.add(mn)
                for i in FSMALL:
                    for j in FSMALL:
                        for k in FSMALL:
                            g.add(mn, f"f4, {vals[i][0]}, {vals[j][0]}, {vals[k][0]}",
                                  [vals[i][1], vals[j][1], vals[k][1]],
                                  [fload("f1", 0), fload("f2", 1), fload("f3", 2), f"{mn} f4, f1, f2, f3"],
                                  kind="fp")
            if mn in ("fadd.d", "fmul.d", "fadd.s", "fmul.s"):   # E3: explicit rounding modes
                for rmode in ("rne", "rtz", "rdn", "rup", "rmm"):
                    for i in FPAIR:
                        for j in FPAIR:
                            g.add(mn, f"f3, {vals[i][0]}, {vals[j][0]}, {rmode}", [vals[i][1], vals[j][1]],
                                  [fload("f1", 0), fload("f2", 1), f"{mn} f3, f1, f2, {rmode}"], kind="fp")
            if mn in ("fcvt.l.d", "fcvt.w.d", "fcvt.l.s"):
                for rmode in ("rne", "rtz", "rdn", "rup", "rmm"):
                    for name, bits in vals:
                        g.add(mn, f"x5, {name}, {rmode}", [bits], [fload("f1", 0), f"{mn} x5, f1, {rmode}"], kind="fp")
            if c == 16:      # unary FP -> FP
                covered.add(mn)
                src = FS if mn == "fcvt.d.s" else FD if mn == "fcvt.s.d" else vals
                sl = "flw" if mn == "fcvt.d.s" else "fld" if mn == "fcvt.s.d" else fl
                for name, bits in src:
                    g.add(mn, f"f3, {name}", [bits], [f"{sl} f1, 0(x10)", f"{mn} f3, f1"], kind="fp")
                g.add(mn, "rd=f31", [src[2][1]], [f"{sl} f1, 0(x10)", f"{mn} f31, f1"], kind="fp")
            elif c == 18:    # integer -> FP
                covered.add(mn)
                for i, a in enumerate(V):
                    g.add(mn, f"f3, x6={VNAME[i]}", [a], ["ld x6, 0(x10)", f"{mn} f3, x6"], kind="fp")
            elif c == 20:    # FP -> integer
                covered.add(mn)
                for name, bits in vals:
                    g.add(mn, f"x5, {name}", [bits], [fload("f1", 0), f"{mn} x5, f1"], kind="fp")
                g.add(mn, "rd=x0", [vals[2][1]], [fload("f1", 0), f"{mn} x0, f1"], kind="fp")
            elif c == 22:    # compare
                covered.add(mn)
                for i in FPAIR:
                    for j in FPAIR:
                        g.add(mn, f"x5, {vals[i][0]}, {vals[j][0]}", [vals[i][1], vals[j][1]],
                              [fload("f1", 0), fload("f2", 1), f"{mn} x5, f1, f2"], kind="fp")
            elif c == 23:    # FP load/store
                covered.add(mn)
                if mn in ("flw", "fld"):
                    for name, bits in vals:
                        g.add(mn, f"f3, {name}", [bits], [f"{mn} f3, 0(x10)"], kind="fp")
                else:
                    for name, bits in vals:
                        g.add(mn, f"{name}, 0(x11)", [bits], [fload("f1", 0), f"{mn} f1, 0(x11)"], kind="fp")
    covered |= e4_programs(g)
    p6_programs(g)
    e5_programs(g)
    with open(os.path.join(HERE, "batch.s"), "w") as f:
        for pid, text in g.progs:
            f.write(f"### {pid}\n{text}")
    json.dump(g.manifest, open(os.path.join(HERE, "manifest.json"), "w"))
    excluded = {k: [m for m in v if m in allm] for k, v in EXCLUDED.items()}
    rest = [m for m in allm if m not in covered and not any(m in v for v in excluded.values())]
    json.dump({"all": allm, "covered": sorted(covered), "excluded": excluded, "uncovered": rest},
              open(os.path.join(HERE, "coverage.json"), "w"), indent=1)
    print(f"{len(allm)} mnemonics, {len(covered)} swept, {len(g.progs)} programs; uncovered: {rest}")

def _load_e4(name):
    """The E4 generators' oracles (written from the spec); their own output is discarded."""
    import contextlib, importlib.util, io
    path = os.path.join(TABLES, f"{name}.py")
    spec = importlib.util.spec_from_file_location("e4_" + name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        spec.loader.exec_module(mod)
    return mod


def e4_programs(g):
    """E4: fdiv by zero (75), float->int bounds (73), fclass (52), CSR forms and pseudos (104).
    Oracle keys: xN = register, mN = scratch dword N."""
    fd, fc, fk, cs = (_load_e4(n) for n in ("gen_fdiv", "gen_fcvt_int", "gen_fclass", "gen_csr"))
    done = set()
    for w, tab, mn, ld in ((32, fd.S, "fdiv.s", "flw"), (64, fd.D, "fdiv.d", "fld")):
        for a, b in fd.PAIRS:
            g.add(mn, f"E4 f3, {a}, {b}", [tab[a], tab[b]],
                  [f"{ld} f1, 0(x10)", f"{ld} f2, 8(x10)", f"{mn} f3, f1, f2", "fsd f3, 0(x11)"],
                  oracle={"m0": fd.fdiv(tab[a], tab[b], w)}, kind="fp")
        done.add(mn)
    for fmt in ("s", "d"):
        for t in ("w", "wu", "l", "lu"):
            mn = f"fcvt.{t}.{fmt}"
            for mode in ("rne", "rtz", "rdn", "rup", "rmm"):
                for b in fc.inputs(fmt):
                    if mode == "rmm" and fc.is_tie(b, fmt):
                        continue
                    g.add(mn, f"E4 x5, {b:#x}, {mode}", [b],
                          [("flw" if fmt == "s" else "fld") + " f1, 0(x10)", f"{mn} x5, f1, {mode}"],
                          oracle={"x5": fc.cvt(b, fmt, t, mode)}, kind="fp")
            done.add(mn)
    for w, vals, mn, ld in ((32, fk.S, "fclass.s", "flw"), (64, fk.D, "fclass.d", "fld")):
        for b in vals:
            g.add(mn, f"x5, {b:#x}", [b], [f"{ld} f1, 0(x10)", f"{mn} x5, f1"],
                  oracle={"x5": fk.fclass(b, w)}, kind="fp")
        done.add(mn)
    forms = {"csrrw": "w", "csrrs": "s", "csrrc": "c", "csrrwi": "w", "csrrsi": "s", "csrrci": "c"}
    for c in ("fflags", "frm", "fcsr"):
        for start in (0x00, 0x5A, 0xFF):
            for form in forms:
                srcs = (0, 1, 21, 31) if form.endswith("i") else (0, 6)
                for src in srcs:
                    for rd in (5, 0):
                        m = cs.Fcsr(); m.write("fcsr", start)
                        sv = src if form.endswith("i") else (0xA5 if src else 0)
                        old = m.op(form, c, sv, src == 0, rd == 0)
                        opnd = str(src) if form.endswith("i") else f"x{src}"
                        orc = {"x8": m.read("fcsr")}
                        if rd:
                            orc["x5"] = old
                        g.add(form, f"E4 x{rd}, {c}, {opnd} from {start:#x}", [start, 0xA5],
                              ["ld x7, 0(x10)", "ld x6, 8(x10)", "csrrw x0, fcsr, x7",
                               f"{form} x{rd}, {c}, {opnd}", "csrrs x8, fcsr, x0"],
                              oracle=orc, kind="csr")
                done.add(form)
        for pseudo, base, operand in (("csrr x5, " + c, "csrrs", None), ("csrw %s, x6" % c, "csrrw", 0xA5),
                                      ("csrs %s, x6" % c, "csrrs", 0xA5), ("csrc %s, x6" % c, "csrrc", 0xA5),
                                      ("csrwi %s, 13" % c, "csrrwi", 13), ("csrsi %s, 13" % c, "csrrsi", 13),
                                      ("csrci %s, 13" % c, "csrrci", 13)):
            m = cs.Fcsr(); m.write("fcsr", 0x5A)
            old = m.op(base, c, operand or 0, operand is None, pseudo.startswith("csrr "))
            orc = {"x8": m.read("fcsr")}
            if pseudo.startswith("csrr "):
                orc["x5"] = old
            g.add(pseudo.split()[0], "E4 " + pseudo, [0x5A, 0xA5],
                  ["ld x7, 0(x10)", "ld x6, 8(x10)", "csrrw x0, fcsr, x7", pseudo, "csrrs x8, fcsr, x0"],
                  oracle=orc, kind="csr")
            done.add(pseudo.split()[0])
    return done


def e5_programs(g):
    """E5 (72): fflags after each F/D op, from lib/fpo.py via gen_fflags's groups.
    fflags is cleared before the op and read right after it (x8)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("gen_fflags", os.path.join(TABLES, "gen_fflags.py"))
    G = importlib.util.module_from_spec(spec); spec.loader.exec_module(G)
    n = 0
    for grp in [G.arith("s"), G.arith("d"), G.fmas("s"), G.fmas("d"), G.compares("s"), G.compares("d"),
                G.conversions("s"), G.conversions("d"), G.narrowing()]:
        for loads, op, flags, _ in grp.items:
            p = G.Program()
            for fn in loads:
                fn(p)
            g.add(op.split()[0], "E5 " + op, p.table, p.body + ["csrrwi x0, fflags, 0", op, "csrrs x8, fflags, x0"],
                  oracle={"x8": flags}, kind="fp")
            n += 1
    print(f"e5: {n} flag programs", file=sys.stderr)


def p6_programs(g):
    """E4 Part 6: single precision against an exact binary32 oracle (report only)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("p6", os.path.join(vpaths.LIB, "p6_oracle.py"))
    o = importlib.util.module_from_spec(spec); spec.loader.exec_module(o)
    from fractions import Fraction
    F = Fraction
    vals = [1.0, 1.0 + 2 ** -23, 3.0, 0.1, -2.5, 7.0, 1.0 / 3.0, 2.0 ** -149, 1.5e-38, -1e20, 2.0 ** -24, 1e10]
    bits = [o.f2b(v) for v in vals]
    ops = {"fadd.s": lambda a, b: a + b, "fsub.s": lambda a, b: a - b,
           "fmul.s": lambda a, b: a * b, "fdiv.s": lambda a, b: a / b}
    modes = [("dyn-rne", 0, ""), ("dyn-rtz", 1, ""), ("dyn-rup", 3, ""),
             ("rtz", None, ", rtz"), ("rdn", None, ", rdn"), ("rup", None, ", rup"), ("rmm", None, ", rmm")]
    RM = {"dyn-rne": "rne", "dyn-rtz": "rtz", "dyn-rup": "rup", "rtz": "rtz", "rdn": "rdn", "rup": "rup", "rmm": "rmm"}
    def pre(frm):
        return [f"csrwi frm, {frm}"] if frm is not None else []
    for mn, f in ops.items():
        for a in bits:
            for b in bits:
                q = f(F(o.b2f(a)), F(o.b2f(b)))
                if q != 0 and abs(q) > F(o.b2f(0x7F7FFFFF)):
                    continue
                for name, frm, suffix in modes:
                    if RM[name] == "rmm" and o.is_tie(q):
                        continue
                    exp = o.round_f32(q, RM[name])
                    if q == 0 and mn in ("fadd.s", "fsub.s"):
                        # IEEE 754 6.3: an exact zero sum of nonzero operands is +0,
                        # except under roundTowardNegative, where it is -0.
                        exp = 0x80000000 if RM[name] == "rdn" else 0
                    g.add(mn, f"P6 {a:#x}, {b:#x}, {name}", [a, b],
                          pre(frm) + ["flw f1, 0(x10)", "flw f2, 8(x10)", f"{mn} f3, f1, f2{suffix}", "fsd f3, 0(x11)"],
                          oracle={"m0": exp}, kind="p6")
    for a in bits:
        if o.b2f(a) < 0:
            continue
        for name, frm, suffix in modes:
            g.add("fsqrt.s", f"P6 {a:#x}, {name}", [a],
                  pre(frm) + ["flw f1, 0(x10)", f"fsqrt.s f3, f1{suffix}", "fsd f3, 0(x11)"],
                  oracle={"m0": o.sqrt_f32(a, RM[name])}, kind="p6")
    ints = [0, 1, -1, 16777217, -16777219, 2 ** 31 - 1, -2 ** 31, 2 ** 53 + 1, 2 ** 63 - 1, -2 ** 63, 123456789, 33554435]
    for mn, lo, hi in (("fcvt.s.w", -2 ** 31, 2 ** 31 - 1), ("fcvt.s.l", -2 ** 63, 2 ** 63 - 1)):
        for v in ints:
            if not lo <= v <= hi:
                continue
            for name, frm, suffix in modes:
                if RM[name] == "rmm" and o.is_tie(F(v)):
                    continue
                g.add(mn, f"P6 {v}, {name}", [v & (2 ** 64 - 1)],
                      pre(frm) + ["ld x6, 0(x10)", f"{mn} f3, x6{suffix}", "fsd f3, 0(x11)"],
                      oracle={"m0": o.round_f32(F(v), RM[name])}, kind="p6")


def run():
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    def one(m):
        out = os.path.join(HERE, "out", m + ".txt")
        subprocess.run([HARNESS, BIN, "run", m, os.path.join(HERE, "batch.s"), out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return m, sum(1 for _ in open(out))
    with ThreadPoolExecutor(5) as ex:
        for m, n in ex.map(one, MODELS):
            print(m, n, "results")

def parse(path):
    res = {}
    for line in open(path):
        pid, kv, vals = parse_line(line)
        if kv is None:  # the temp path differs per process; keep the message only
            res[pid] = {"err": " ".join(x for x in vals.split() if not x.startswith("/tmp/"))}
            continue
        res[pid] = {"pc": int(kv["pc"], 16), "x": vals[0:32], "f": vals[32:64],
                    "fcsr": vals[64], "mem": vals[65:97]}
    return res

def compare():
    man = json.load(open(os.path.join(HERE, "manifest.json")))
    out = {m: parse(os.path.join(HERE, "out", m + ".txt")) for m in MODELS}
    div, orc = [], []
    for pid, info in man.items():
        ref = out["rvss"].get(pid)
        # oracle vs rvss
        if info["oracle"] and ref and "err" not in ref:
            for k, v in info["oracle"].items():
                got = ref["x"][int(k[1:])] if k[0] == "x" else ref["mem"][int(k[1:])]
                if got != v:
                    orc.append((info["mn"], info["ops"], k, hex(v), hex(got)))
        for m in MODELS[1:]:
            got = out[m].get(pid)
            if info["kind"] == "li" and m == "nh-nf":
                continue  # li's own chain: by design (77)
            if ref is None or got is None:
                div.append((info["mn"], info["ops"], m, "missing", "", ""))
                continue
            if "err" in ref or "err" in got:
                if ref.get("err") != got.get("err"):
                    div.append((info["mn"], info["ops"], m, "assemble", ref.get("err", "ok"), got.get("err", "ok")))
                continue
            for field in ("x", "f", "mem"):
                for i, (a, b) in enumerate(zip(ref[field], got[field])):
                    if a != b:
                        div.append((info["mn"], info["ops"], m, f"{field}{i}", hex(a), hex(b)))
            if ref["fcsr"] != got["fcsr"]:
                div.append((info["mn"], info["ops"], m, "fcsr", hex(ref["fcsr"]), hex(got["fcsr"])))
    with open(os.path.join(HERE, "divergences.tsv"), "w") as f:
        for d in div: f.write("\t".join(map(str, d)) + "\n")
    with open(os.path.join(HERE, "oracle.tsv"), "w") as f:
        for d in orc: f.write("\t".join(map(str, d)) + "\n")
    errs = sorted({(man[p]["mn"], r["err"][:80]) for p, r in out["rvss"].items() if "err" in r})
    print(f"{len(div)} model divergences, {len(orc)} oracle mismatches, {len(errs)} distinct assemble errors")
    for e in errs: print("  ERR", *e)

if __name__ == "__main__":
    {"lists": lists, "gen": gen, "run": run, "compare": compare, "table": lambda: None}[sys.argv[1]]()

# ---- classification and the table (appended after the first run) ----------
def classify(mn, field):
    if mn == "srai":
        return "106 new"
    if mn.endswith(".d") or ".d." in mn or mn in ("fcvt.s.d", "fcvt.d.s"):
        return "108 new (72 family)" if field == "fcsr" else "107 new"
    return "UNCLASSIFIED"

def table():
    rows = {}
    for line in open(os.path.join(HERE, "divergences.tsv")):
        mn, ops, m, field, a, b = line.rstrip("\n").split("\t")
        key = (mn, classify(mn, field))
        rows.setdefault(key, {mm: set() for mm in MODELS[1:]})[m].add(ops)
    out = ["| mnemonic | class | " + " | ".join(MODELS[1:]) + " |",
           "|---|---|" + "---|" * 4]
    for (mn, cls), per in sorted(rows.items()):
        out.append(f"| {mn} | {cls} | " + " | ".join(str(len(per[m])) for m in MODELS[1:]) + " |")
    man = json.load(open(os.path.join(HERE, "manifest.json")))
    total = {m: sum(len(per[m]) for per in rows.values()) for m in MODELS[1:]}
    out.append("| **programs diverging** | | " + " | ".join(str(total[m]) for m in MODELS[1:]) + " |")
    open(os.path.join(HERE, "table.md"), "w").write("\n".join(out) + "\n")
    print("\n".join(out))
    print(f"\n{len(man)} programs; counts are PROGRAMS that diverge from rvss (a program may differ in several fields)")

if __name__ == "__main__" and sys.argv[1] == "table":
    table()
