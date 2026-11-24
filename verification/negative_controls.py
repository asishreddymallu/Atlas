#!/usr/bin/env python3
"""Negative controls: proof that each checker can fail.

Each control copies one sweep's fresh output (from verification/out), plants
exactly one wrong value in the copy, reruns that sweep's own comparison on the
copy, and requires the comparison to report exactly one more problem than it
reports on the untouched output. A checker that passes everything would fail
here. The real output is never modified.

Run after the sweeps (scripts/run_verification.sh does). Prints one line per
control and "controls N, caught M"; exits 1 if any control is missed.
"""
import json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
sys.path.insert(0, os.path.join(HERE, "cache"))
sys.path.insert(0, os.path.join(HERE, "tables"))
import vpaths
from probeio import parse_line


def run(script, arg, out_dir):
    env = dict(os.environ, ATLAS_VERIFY_OUT=out_dir)
    r = subprocess.run([sys.executable, os.path.join(HERE, script), arg], capture_output=True, text=True,
                       env=env)
    return r.stdout


def rewrite_line(path, pick, change):
    """Replace the first line for which pick(line) is true with change(line)."""
    lines = open(path).readlines()
    for i, line in enumerate(lines):
        if pick(line):
            lines[i] = change(line)
            open(path, "w").writelines(lines)
            return line.split()[0]
    raise RuntimeError(f"no line to change in {path}")


def isa_oracle(tmp):
    """One rvss register that the ISA oracle predicts is changed by one."""
    shutil.copytree(os.path.join(vpaths.OUT, "isa"), os.path.join(tmp, "isa"))
    count = lambda text: tuple(int(x) for x in re.search(r"(\d+) model divergences, (\d+) oracle", text).groups())
    before = count(run("isa/sweep.py", "compare", tmp))
    man = json.load(open(os.path.join(tmp, "isa", "manifest.json")))
    target = next((pid, k) for pid, info in man.items() if info.get("oracle")
                  for k in info["oracle"] if k[0] == "x" and k != "x0")

    def change(line):
        pid, kv, vals = parse_line(line)
        p = line.split()
        i = 2 + len(kv) + int(target[1][1:])
        p[i] = format(int(p[i], 16) ^ 1, "x")
        return " ".join(p) + "\n"

    pid = rewrite_line(os.path.join(tmp, "isa", "out", "rvss.txt"),
                       lambda l: l.split()[0] == target[0] and l.split()[1] == "OK", change)
    after = count(run("isa/sweep.py", "compare", tmp))
    return f"ISA: rvss {pid} {target[1]} flipped", before[1], after[1]


def hazard_cycles(tmp):
    """One h-f cycle count is changed by one."""
    shutil.copytree(os.path.join(vpaths.OUT, "hazard"), os.path.join(tmp, "hazard"))
    count = lambda text: int(re.search(r"cycles != analytic: (\d+) programs", text).group(1))
    before = count(run("hazard/hazgen.py", "compare", tmp))

    def change(line):
        return re.sub(r"cycles=(\d+)", lambda m: f"cycles={int(m.group(1)) + 1}", line, count=1)

    pid = rewrite_line(os.path.join(tmp, "hazard", "out", "h-f.txt"), lambda l: " OK " in l, change)
    after = count(run("hazard/hazgen.py", "compare", tmp))
    return f"hazard: h-f {pid} cycles+1", before, after


def cache_hits(tmp):
    """One L1 hit count from the probe is changed by one."""
    import run_all
    res = run_all.load_probe_json(vpaths.out("cache", "probe.json"))
    exp = run_all.expectations()
    before = run_all.compare(res, exp, quiet=True)
    key = ("stride_1", "dm256", "rvss")
    res[key]["L1D"]["hits"] += 1
    after = run_all.compare(res, exp, quiet=True)
    return "cache: rvss stride_1 dm256 L1 hits+1", before, after


def asm_word(tmp):
    """One machine word in one assembler dump is changed."""
    src = os.path.join(vpaths.OUT, "asm", "dumps")
    dst = os.path.join(tmp, "dumps")
    shutil.copytree(src, dst)
    expected = os.path.join(HERE, "asm", "expected.sha256")

    def count():
        r = subprocess.run(["sha256sum", "--quiet", "-c", expected], cwd=dst, capture_output=True, text=True)
        return sum(1 for l in r.stdout.splitlines() if l.endswith("FAILED"))

    before = count()
    name = "code_examples_ex_mem_fwd.s.txt"
    path = os.path.join(dst, name)
    text = open(path).read()
    word = re.search(r"^  ([0-9a-f]{8})$", text, re.M)
    flipped = format(int(word.group(1), 16) ^ 0x80, "08x")
    open(path, "w").write(text.replace(word.group(0), "  " + flipped, 1))
    return f"asm: one word in {name}", before, count()


def table_value(tmp):
    """One expected number in a copy of a committed table is changed."""
    import check_tables
    target = os.path.join(vpaths.ROOT, "test", "cache_lab_cases.inc")
    before = 0 if check_tables.check("cache/gen_cases.py", target)[0] == "MATCH" else 1
    copy = os.path.join(tmp, "cache_lab_cases.inc")
    text = open(target).read()
    line = next(l for l in text.splitlines() if l.startswith('{"stride_1"'))
    fields = line.split(", ")
    fields[8] = str(int(fields[8]) + 1)  # L1D hits
    open(copy, "w").write(text.replace(line, ", ".join(fields), 1))
    after = 0 if check_tables.check("cache/gen_cases.py", copy)[0] == "MATCH" else 1
    return "tables: one L1 hit in cache_lab_cases.inc", before, after


def main():
    caught = 0
    controls = [isa_oracle, hazard_cycles, cache_hits, asm_word, table_value]
    for control in controls:
        with tempfile.TemporaryDirectory(prefix="atlas-negctl-") as tmp:
            try:
                what, before, after = control(tmp)
            except Exception as e:  # a control that cannot run counts as missed
                print(f"MISSED  {control.__name__}: could not run ({e})")
                continue
        ok = after == before + 1
        caught += ok
        print(f"{'CAUGHT' if ok else 'MISSED'}  {what}: problems {before} -> {after}")
    print(f"controls {len(controls)}, caught {caught}")
    sys.exit(0 if caught == len(controls) else 1)


if __name__ == "__main__":
    main()
