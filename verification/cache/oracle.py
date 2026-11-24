"""Expected cache statistics for a cache-lab program (RVSS order: fetch, then data, per instruction)."""
import cachemodel, miniiss

CONFIGS = {  # name: (sets, ways, line, policy, write_back, write_allocate)
    "dm256":     (16, 1, 16, "LRU", 0, 0),
    "2w256lru":  (8, 2, 16, "LRU", 0, 0),
    "4w256lru":  (4, 4, 16, "LRU", 0, 0),
    "2w256fifo": (8, 2, 16, "FIFO", 0, 0),
    "dm1024":    (64, 1, 16, "LRU", 0, 0),
    "4w512wbwa": (4, 4, 32, "LRU", 1, 1),
    "2w128fifo": (8, 2, 8, "FIFO", 0, 0),
}
ICACHE = (64, 4, 16, "LRU", 0, 0)
L2 = (32, 4, 64, "LRU", 0, 0)

def mk(spec, nxt, name):
    s, w, l, p, wb, wa = spec
    return cachemodel.Cache(s, w, l, p, bool(wb), bool(wa), nxt, name)

def expect(src_text, l1spec):
    r = miniiss.run(src_text)
    l2 = mk(L2, cachemodel.Memory(), "L2")
    ic = mk(ICACHE, l2, "I")
    dc = mk(l1spec, l2, "L1D")
    for kind, a, sz, rw in r["events"]:
        (ic if kind == "F" else dc).access(a, sz, rw)
    return dict(L1D=dc.stats(), I=ic.stats(), L2=l2.stats(), x8=r["regs"][8], steps=r["steps"])
