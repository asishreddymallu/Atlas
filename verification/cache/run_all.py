"""The L1 cache sweep: every cache-lab program x config x model on the probe,
compared with the independent Python model (oracle.py = cachemodel.py + miniiss.py).

usage: run_all.py [models...]          (default: all five)
writes verification/out/cache/probe.json (raw probe counts) and results.csv;
prints "rows N mismatches M" and exits 1 if M > 0.

The L1 data cache is compared on every model. The instruction cache and L2
are compared on rvss only: the pipelines fetch in a different order."""
import csv, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "lib"))
import oracle
import vpaths

REPO = vpaths.ROOT
ALL_MODELS = ["rvss", "nh-nf", "nh-f", "h-nf", "h-f"]
PROBE = os.environ.get("CACHEPROBE", vpaths.probe("atlas_verify_cacheprobe"))
BENCH_DIR = os.path.join(REPO, "examples", "cache_lab")
BENCH = sorted(f[:-2] for f in os.listdir(BENCH_DIR) if f.endswith(".s"))


def probe(m, path, spec):
    s = ",".join(str(v) for v in spec)
    out = subprocess.run([vpaths.HARNESS, PROBE, m, path, s], capture_output=True, text=True,
                         timeout=600).stdout
    r = {}
    for ln in out.splitlines():
        f = ln.split()
        if f and f[0] in ("L1D", "I", "L2"):
            r[f[0]] = dict(hits=int(f[1]), misses=int(f[2]), evc=int(f[3]), evd=int(f[4]),
                           writebacks=int(f[5]))
        elif f and f[0] == "end":
            r["end"] = dict(kv.split("=") for kv in f[1:])
    return r


def expectations():
    return {(b, c): oracle.expect(open(os.path.join(BENCH_DIR, b + ".s")).read(), oracle.CONFIGS[c])
            for b in BENCH for c in oracle.CONFIGS}


def compare(res, exp, csv_path=None, quiet=False):
    """res: {(bench, config, model): probe dict}. Returns the mismatch count."""
    bad = 0
    say = (lambda *a: None) if quiet else print
    rows = []
    for (b, c, m), r in sorted(res.items()):
        e = exp[(b, c)]
        if int(r["end"]["x8"]) != e["x8"]:
            say("X8 MISMATCH", b, c, m, r["end"]["x8"], e["x8"]); bad += 1
        for lvl in ("L1D", "I", "L2"):
            s = r[lvl]
            ev = s["evc"] if s["evc"] >= 0 else s["evd"]
            tot = s["hits"] + s["misses"]
            rows.append([b, c, m, lvl, s["hits"], s["misses"], ev, f"{s['hits'] / tot:.4f}" if tot else "0"])
            if s["evc"] >= 0 and s["evc"] != s["evd"]:
                say("COUNTER!=DIFF", b, c, m, lvl, s["evc"], s["evd"])
            o = e[lvl]
            got = (s["hits"], s["misses"], ev)
            want = (o["hits"], o["misses"], o["evictions"])
            compared = lvl == "L1D" or m == "rvss"
            if compared and got != want:
                say("MISMATCH", b, c, m, lvl, "sim", got, "oracle", want); bad += 1
    if csv_path:
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["benchmark", "config", "model", "level", "hits", "misses", "evictions", "hit_rate"])
            w.writerows(rows)
    return bad


def load_probe_json(path):
    return {tuple(k.split("|")): v for k, v in json.load(open(path)).items()}


def main():
    models = sys.argv[1:] or ALL_MODELS
    jobs = [(b, c, m) for b in BENCH for c in oracle.CONFIGS for m in models]

    def work(j):
        b, c, m = j
        return j, probe(m, os.path.join(BENCH_DIR, b + ".s"), oracle.CONFIGS[c])

    with ThreadPoolExecutor(4) as ex:
        res = dict(ex.map(work, jobs))
    json.dump({"|".join(k): v for k, v in res.items()}, open(vpaths.out("cache", "probe.json"), "w"),
              indent=0, sort_keys=True)
    bad = compare(res, expectations(), vpaths.out("cache", "results.csv"))
    print("rows", len(res), "mismatches", bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
