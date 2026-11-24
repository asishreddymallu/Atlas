#!/usr/bin/env python3
"""Stage T1: expected pipeline timing for generated programs, written from the
pipeline rules, never from the simulator.

The rules (the textbook 5-stage pipeline as Atlas builds it; the recurrence is
E6's analytic cycle model, verification/hazard/hazgen.py:cycles):

  t_i  = the cycle in which instruction i LEAVES ID (decodes for real).
  arrival_i = t_{i-1} + 1 + penalty_{i-1}   (first instruction: 2)
  t_i  = max(arrival_i, t_j + 3)  for every earlier producer j of a register i reads  (h-nf)
       = max(arrival_i, t_j + 2)  for every earlier LOAD j of a register i reads      (h-f)
       = arrival_i                                                                  (nh-nf, nh-f)
  penalty = 2 after jal/jalr (resolved in EX), 3 after a taken branch (resolved in
            MEM), 0 otherwise.
  stage cycles: ID from arrival_i to t_i (t_i - arrival_i stall cycles),
                EX t_i+1, MEM t_i+2, WB t_i+3.
  IF: the first instruction at 1; after a redirect at arrival_i - 1 (the target is
      fetched the cycle after the redirect); otherwise when its predecessor leaves
      IF, i.e. at arrival_{i-1}.
  total cycles = t_last + 3 (the last WB; the run ends drained).

Usage: gen_timing.py > test/timing_cases.inc
"""
import random

MODELS = ["nh_nf", "nh_f", "h_nf", "h_f"]
POOL = ["x5", "x6", "x7", "x8"]
LOADS = {"lw", "ld"}


def rw(ins):
    """(written, read) registers of one instruction, x0 dropped."""
    op, *a = ins.replace(",", " ").split()
    w, r = set(), set()
    if op in ("addi", "slli"):
        w, r = {a[0]}, {a[1]}
    elif op in ("add", "sub", "and", "or", "xor"):
        w, r = {a[0]}, {a[1], a[2]}
    elif op in LOADS:
        w, r = {a[0]}, {a[1].split("(")[1].rstrip(")")}
    elif op == "sw":
        r = {a[0], a[1].split("(")[1].rstrip(")")}
    elif op in ("beq", "bne"):
        r = {a[0], a[1]}
    elif op == "jal":
        w = {a[0]}
    elif op == "lui":
        w = {a[0]}
    return {x for x in w if x != "x0"}, {x for x in r if x != "x0"}


def timing(stream, model):
    """stream: [(pc, ins, taken)] in execution order -> (cycles, stalls, rows)."""
    t, info, rows = [], [], []
    prev_pen, prev_arrival = 0, None
    for k, (pc, ins, taken) in enumerate(stream):
        w, r = rw(ins)
        op = ins.split()[0]
        arrival = 2 if not t else t[-1] + 1 + prev_pen
        ti = arrival
        for (pw, pload), tj in zip(info, t):
            if pw & r:
                if model == "h_nf":
                    ti = max(ti, tj + 3)
                elif model == "h_f" and pload:
                    ti = max(ti, tj + 2)
        if k == 0:
            fetch = 1
        elif prev_pen:
            fetch = arrival - 1
        else:
            fetch = prev_arrival
        rows.append((pc, fetch, arrival, ti + 1, ti + 2, ti + 3, ti - arrival))
        t.append(ti)
        info.append((w, op in LOADS))
        prev_arrival = arrival
        prev_pen = 2 if op == "jal" else (3 if taken else 0)
    return t[-1] + 3, sum(x[6] for x in rows), rows


def execute(prog):
    """prog: [(ins or None, label)] -> executed stream [(pc, ins, taken)]."""
    labels = {}
    for i, (ins, lab) in enumerate(prog):
        if lab:
            labels[lab] = i
    out, i = [], 0
    while i < len(prog):
        ins, _ = prog[i]
        op = ins.split()[0]
        nxt = i + 1
        taken = False
        if op == "beq":  # beq xa, xa, L: always taken
            nxt, taken = labels[ins.split(",")[-1].strip()], True
        elif op == "jal":
            nxt = labels[ins.split(",")[-1].strip()]
        out.append((4 * i, ins, taken))
        i = nxt
    return out


def gen(rng, n):
    body = []
    k = 0
    while len(body) < n:
        c = rng.random()
        d, s1, s2 = rng.choice(POOL), rng.choice(POOL), rng.choice(POOL)
        if c < 0.30:
            body.append([f"add {d}, {s1}, {s2}"])
        elif c < 0.45:
            body.append([f"addi {d}, {s1}, {rng.randint(-8, 8)}"])
        elif c < 0.62:
            body.append([f"lw {d}, {4 * rng.randint(0, 7)}(x9)"])
        elif c < 0.70:
            body.append([f"sw {s1}, {4 * rng.randint(0, 7)}(x9)"])
        elif c < 0.77:
            body.append(["nop"])
        else:
            skip = rng.randint(1, 3)
            kind = rng.choice(["beq", "bne", "jal"])
            lab = f"L{k}"
            k += 1
            head = {"beq": f"beq {s1}, {s1}, {lab}", "bne": f"bne {s1}, {s1}, {lab}",
                    "jal": f"jal x0, {lab}"}[kind]
            block = [head] + [f"addi {rng.choice(POOL)}, {rng.choice(POOL)}, 1" for _ in range(skip)]
            block.append((f"add {d}, {s1}, {s2}", lab))
            body.append(block)
    # x9 is the data base; four nops let it reach WB before any use, so the
    # models without hazard handling never store through a stale x9.
    prog = [("lui x9, 0x10000", None)] + [("nop", None)] * 4
    for blk in body:
        for x in blk:
            prog.append(x if isinstance(x, tuple) else (x, None))
    prog.append(("addi x1, x0, 1", None))  # a plain last instruction
    return prog


def source(prog):
    lines = [".data", "D: .zero 64", ".text"]
    for ins, lab in prog:
        lines.append((lab + ": " if lab else "    ") + ins)
    return "\n".join(lines) + "\n"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    rng = random.Random(20261119)
    print("// Generated by verification/timing/gen_timing.py from the pipeline rules")
    print("// (E6's analytic recurrence). Do not edit; regenerate.")
    print("// { name, source, { per model nh_nf, nh_f, h_nf, h_f:")
    print("//   { cycles, stalls, { {pc, IF, firstID, EX, MEM, WB, stalls}, ... } } } }")
    for p in range(64):
        prog = gen(rng, rng.randint(6, 16))
        stream = execute(prog)
        print("{" + cstr(f"gen{p:02d}") + ", " + cstr(source(prog)) + ", {")
        for m in MODELS:
            cyc, st, rows = timing(stream, m)
            rs = ", ".join("{%d,%d,%d,%d,%d,%d,%d}" % r for r in rows)
            print(f"  {{{cyc}u, {st}u, {{{rs}}}}},  // {m}")
        print("}},")


if __name__ == "__main__":
    main()
