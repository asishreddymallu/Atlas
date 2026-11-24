"""Textbook set-associative cache, written from the definition (not from the simulator).

address -> offset = addr mod line, index = (addr div line) mod sets, tag = addr div (line*sets).
A lookup hits iff some valid way in the set holds the tag. A miss that allocates fills an
invalid way if one exists (lowest-numbered), else the replacement policy picks a victim;
replacing a valid line is an eviction (and a write-back if the line is dirty under write-back).

Policies: LRU (victim = least recently used, a fill counts as a use), FIFO (victim = earliest
filled; hits do not reorder).
Write hit: write-through forwards the write to the next level; write-back marks the line dirty.
Write miss: write-allocate fills the line first and then treats it as a hit (no second count);
no-write-allocate forwards the write to the next level and leaves the cache unchanged.
Every access whose bytes lie in one line counts exactly one hit or one miss.
"""


class Cache:
    def __init__(self, sets, ways, line, policy="LRU", write_back=False,
                 write_allocate=False, next_level=None, name="cache"):
        assert sets & (sets - 1) == 0 and line & (line - 1) == 0
        self.sets, self.ways, self.line = sets, ways, line
        self.policy = policy
        self.write_back, self.write_allocate = write_back, write_allocate
        self.next = next_level
        self.name = name
        # each way: dict(valid, tag, dirty, stamp_use, stamp_fill)
        self.s = [[dict(valid=False, tag=0, dirty=False, use=0, fill=0)
                   for _ in range(ways)] for _ in range(sets)]
        self.clock = 0
        self.hits = self.misses = self.evictions = self.writebacks = 0

    @property
    def size(self):
        return self.sets * self.ways * self.line

    def split(self, addr):
        block = addr // self.line
        return block // self.sets, block % self.sets, addr % self.line

    def _find(self, idx, tag):
        for w, l in enumerate(self.s[idx]):
            if l["valid"] and l["tag"] == tag:
                return w
        return None

    def _fill(self, addr):
        tag, idx, _ = self.split(addr)
        ways = self.s[idx]
        w = next((i for i, l in enumerate(ways) if not l["valid"]), None)
        if w is None:
            key = "use" if self.policy == "LRU" else "fill"
            w = min(range(self.ways), key=lambda i: ways[i][key])
            self.evictions += 1
            v = ways[w]
            if v["dirty"] and self.write_back:
                self.writebacks += 1
                if self.next:
                    vaddr = (v["tag"] * self.sets + idx) * self.line
                    self.next.access(vaddr, self.line, "W")
        if self.next:
            self.next.access((addr // self.line) * self.line, self.line, "R")
        self.clock += 1
        ways[w].update(valid=True, tag=tag, dirty=False, use=self.clock, fill=self.clock)
        return w

    def access(self, addr, size, kind):
        """kind 'R' or 'W'. Returns True on hit."""
        assert (addr % self.line) + size <= self.line, "access straddles a line"
        tag, idx, _ = self.split(addr)
        w = self._find(idx, tag)
        hit = w is not None
        if hit:
            self.hits += 1
            self.clock += 1
            self.s[idx][w]["use"] = self.clock
        else:
            self.misses += 1
            if kind == "R" or self.write_allocate:
                w = self._fill(addr)
            else:  # write miss, no-write-allocate
                if self.next:
                    self.next.access(addr, size, "W")
                return False
        if kind == "W":
            if self.write_back:
                self.s[idx][w]["dirty"] = True
            elif self.next:
                self.next.access(addr, size, "W")
        return hit

    def stats(self):
        return dict(hits=self.hits, misses=self.misses, evictions=self.evictions,
                    writebacks=self.writebacks)


class Memory:
    def access(self, addr, size, kind):
        return True


if __name__ == "__main__":
    # self-checks from the definition
    c = Cache(sets=1, ways=2, line=16)
    for a in (0, 16, 0, 32, 16):  # LRU: 0 m,16 m,0 h,32 m (evict 16),16 m (evict 0)
        c.access(a, 4, "R")
    assert c.stats() == dict(hits=1, misses=4, evictions=2, writebacks=0), c.stats()
    f = Cache(sets=1, ways=2, line=16, policy="FIFO")
    for a in (0, 16, 0, 32, 0):  # FIFO: 32 evicts 0 (oldest fill) -> 0 misses again
        f.access(a, 4, "R")
    assert f.stats() == dict(hits=1, misses=4, evictions=2, writebacks=0), f.stats()
    d = Cache(sets=4, ways=1, line=16)  # 1024-byte example geometry check
    assert Cache(sets=64, ways=1, line=16).size == 1024
    assert d.split(0x10 * 5 + 3) == (1, 1, 3)
    print("cachemodel self-checks pass")
