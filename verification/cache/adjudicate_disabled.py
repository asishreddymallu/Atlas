# The four #if 0 cache tests, replayed on the textbook model. The tests' own expectations
# forget that re-reading the evicted line itself evicts the next victim.
from cachemodel import Cache
def run(ways, pol, seq_before, probe_miss, probe_hits):
    c = Cache(1, ways, 4, pol, True, True)
    for a in seq_before: c.access(a, 4, 'R')
    m0 = c.misses; c.access(probe_miss, 4, 'R'); miss_ok = c.misses == m0 + 1
    h0 = c.hits
    for a in probe_hits: c.access(a, 4, 'R')
    return miss_ok, c.hits - h0, c.hits
print("BasicEvictionOrder", run(3, "LRU", [0, 4, 8, 12], 0, [4, 8]), "test expects (True, 2)")
print("AccessPromotesToMostRecent", run(3, "LRU", [0, 4, 8, 0, 4, 12], 8, [0, 4]), "test expects (True, 2)")
print("LRUAgeingAcrossManyCycles", run(3, "LRU", [0, 4, 8] + [4, 8] * 10 + [12], 0, [4, 8]), "test expects (True, 2)")
print("FIFOEvictsFirstInserted", run(2, "FIFO", [0, 4, 0, 8], 0, [4]), "test expects (True, 1)")
