#!/usr/bin/env python3
"""Stage E3 part B (known_issues 110): audit the whole `li` expansion.

  li_audit.py values            -> values.json   (the audit set)
  li_audit.py encode <tag>      -> words-<tag>.txt   (via the sweep probe's words)
  li_audit.py check <tag>       -> decode + emulate every expansion in Python,
                                   compare with the value; counts per value
  li_audit.py counts <a> <b>    -> instruction counts that differ between tags,
                                   for values that were RIGHT under <a>
  li_audit.py run <tag>         -> also execute on rvss, h-f, h-nf, nh-f and
                                   compare x5 with the value

The emulator implements exactly the instructions an li expansion may use
(addi, addiw, lui, slli), written from the spec, not from the simulator.
nh_nf cannot run the dependent chain back to back (known_issues 77), so the
emulation IS its check: it shares the encoder with the other four.
"""
import json, os, random, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from common import u64, s64, s32, sx

import vpaths
from probeio import parse_line
HERE = os.path.dirname(vpaths.out("li", "x"))  # verification/out/li
HARNESS = vpaths.HARNESS
SWEEP = vpaths.probe("atlas_verify_sweep")


def audit_values():
    v = set()
    for c in (0, 1 << 11, 1 << 12, 1 << 31, 1 << 32, 1 << 63):
        for d in range(-3, 4):
            v.add(u64(c + d)); v.add(u64(-(c + d)))
    for x in range(0x7FFFF800, 0x80000000):      # the whole 110 range
        v.add(x); v.add(u64(-x))
    v |= {0x7FFFFFFF, 0xFFFFFFFF, M := (1 << 64) - 1, (1 << 63) - 1, 0x80000000, 0x3FF0000000000000}
    rng = random.Random(20261119)
    for _ in range(2000):
        v.add(rng.getrandbits(64))
    return sorted(v)


def emulate(words):
    x = [0] * 32
    for w in words:
        op, rd, f3, rs1 = w & 0x7F, (w >> 7) & 31, (w >> 12) & 7, (w >> 15) & 31
        imm12 = sx(w >> 20, 12)
        if op == 0b0110111:                                   # lui
            r = u64(s32(w & 0xFFFFF000))
        elif op == 0b0010011 and f3 == 0:                     # addi
            r = u64(x[rs1] + imm12)
        elif op == 0b0010011 and f3 == 1:                     # slli
            r = u64(x[rs1] << ((w >> 20) & 63))
        elif op == 0b0011011 and f3 == 0:                     # addiw
            r = u64(s32(x[rs1] + imm12))
        else:
            raise ValueError(f"unexpected instruction {w:08x} in an li expansion")
        if rd:
            x[rd] = r
    return x[5]


def lit(v):
    return str(s64(v)) if s64(v) < 0 else hex(v)


def main():
    cmd = sys.argv[1]
    if cmd == "values":
        vals = audit_values()
        json.dump(vals, open(os.path.join(HERE, "values.json"), "w"))
        print(len(vals), "values")
        return
    vals = json.load(open(os.path.join(HERE, "values.json")))
    if cmd == "encode":
        batch = os.path.join(HERE, "li_batch.s")
        with open(batch, "w") as f:
            for i, v in enumerate(vals):
                f.write(f"### {i}\n.text\n    li x5, {lit(v)}\n")
        subprocess.run([HARNESS, SWEEP, "words", batch, os.path.join(HERE, f"words-{sys.argv[2]}.txt")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    words = {}
    for line in open(os.path.join(HERE, f"words-{sys.argv[2]}.txt")):
        p = line.split()
        words[int(p[0])] = None if p[1] == "ERR" else [int(w, 16) for w in p[2:]]
    if cmd in ("encode", "check"):
        bad = [(v, words[i]) for i, v in enumerate(vals) if words[i] is None or emulate(words[i]) != v]
        lens = {}
        for i in words:
            if words[i]:
                lens[len(words[i])] = lens.get(len(words[i]), 0) + 1
        print(f"{sys.argv[2]}: {len(vals)} values, {len(bad)} wrong; expansion lengths {dict(sorted(lens.items()))}")
        in110 = [v for v, _ in bad if 0x7FFFF800 <= v <= 0x7FFFFFFF]
        print(f"  wrong in 0x7FFFF800..0x7FFFFFFF: {len(in110)}; elsewhere: {len(bad) - len(in110)}")
        for v, w in bad[:8]:
            print(f"  {v:#018x} -> {emulate(w) if w else 'ERR':#018x}" if w else f"  {v:#x} ERR")
        json.dump([v for v, _ in bad], open(os.path.join(HERE, f"wrong-{sys.argv[2]}.json"), "w"))
    elif cmd == "counts":
        other = {}
        for line in open(os.path.join(HERE, f"words-{sys.argv[3]}.txt")):
            p = line.split()
            other[int(p[0])] = None if p[1] == "ERR" else len(p) - 2
        wrong_before = set(json.load(open(os.path.join(HERE, f"wrong-{sys.argv[2]}.json"))))
        changed = [(v, len(words[i]), other[i]) for i, v in enumerate(vals)
                   if v not in wrong_before and len(words[i]) != other[i]]
        print(f"values right under {sys.argv[2]} whose count changed under {sys.argv[3]}: {len(changed)}")
        for c in changed[:10]:
            print("  ", hex(c[0]), c[1], "->", c[2])
    elif cmd == "run":
        # execute on four models through the sweep probe's run; nh-nf by emulation above
        batch = os.path.join(HERE, "li_run.s")
        with open(batch, "w") as f:
            for i, v in enumerate(vals):
                f.write(f"### {i}\n.text\n    li x5, {lit(v)}\n    nop\n    nop\n    nop\n    nop\n")
        for m in ("rvss", "h-f", "h-nf", "nh-f"):
            out = os.path.join(HERE, f"run-{sys.argv[2]}-{m}.txt")
            subprocess.run([HARNESS, SWEEP, "run", m, batch, out],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            bad = 0
            for line in open(out):
                pid, kv, regs = parse_line(line)
                if kv is None or regs[5] != vals[int(pid)]:
                    bad += 1
            print(f"{sys.argv[2]} {m}: {len(vals)} values, {bad} wrong")


if __name__ == "__main__":
    main()
