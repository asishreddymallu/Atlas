/**
 * @file miss_classifier.h
 * @brief Metric 6: cache misses by cause (the "three Cs"). Qt-free.
 *
 * Fed every access of one cache, in order, with whether the real cache hit:
 *   - compulsory: the first access to that block since the classifier was
 *     reset (the cache's Reset);
 *   - capacity: not compulsory, and a fully associative LRU cache with the
 *     same total number of lines (sets x ways) and the same allocation
 *     policy, fed the same accesses, would also miss;
 *   - conflict: every other miss (including misses the real cache's own
 *     replacement order causes where LRU would have hit).
 * So compulsory + capacity + conflict = the misses fed. Modelled: the
 * fully associative cache is a model, not part of the simulator.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace Atlas
{
struct MissCauses
{
    uint64_t accesses{0};
    uint64_t misses{0};
    uint64_t compulsory{0};
    uint64_t capacity{0};
    uint64_t conflict{0};
    uint64_t distinctBlocks{0};
};

class MissClassifier
{
  public:
    /// Sets the geometry and starts again.
    void configure(size_t sets, size_t ways, size_t lineBytes, bool writeAllocate);
    void reset();
    bool configuredFor(size_t sets, size_t ways, size_t lineBytes, bool writeAllocate) const;

    /// One access of the real cache: its address, whether it was a write,
    /// and whether the real cache hit.
    void access(uint64_t address, bool isWrite, bool hit);

    const MissCauses &causes() const { return m_causes; }

  private:
    size_t m_sets{1}, m_ways{1}, m_lineBytes{1};
    bool   m_writeAllocate{false};
    size_t m_lines{1};
    MissCauses m_causes;
    std::unordered_set<uint64_t> m_seen;
    std::list<uint64_t>          m_lru; ///< most recent first
    std::unordered_map<uint64_t, std::list<uint64_t>::iterator> m_where;
};
} // namespace Atlas
