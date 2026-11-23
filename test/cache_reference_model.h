/**
 * @file cache_reference_model.h
 * @brief A tiny set-associative LRU cache, written for the tests from the
 *        textbook definition and not from the product's code.
 *
 * Address = tag | set | offset. A read miss fills the first empty way, or
 * else replaces the least recently used one (the lowest way on a tie). A
 * hit makes its line the most recently used. A write miss allocates only
 * with write allocate. Only bookkeeping is modelled: valid, tag and use
 * order, and the hit / miss / eviction counts.
 */
#ifndef ATLAS_TEST_CACHE_REFERENCE_MODEL_H
#define ATLAS_TEST_CACHE_REFERENCE_MODEL_H

#include <cstdint>
#include <vector>

namespace Atlas::test_support
{
class ReferenceCache
{
  public:
    struct Way
    {
        bool     valid{false};
        uint64_t tag{0};
        uint64_t lastUse{0};
    };
    struct Outcome
    {
        uint64_t set{0};
        uint64_t tag{0};
        bool     hit{false};
        bool     evicted{false};
        uint64_t evictedTag{0};
    };

    ReferenceCache(uint64_t sets, uint64_t ways, uint64_t lineBytes, bool writeAllocate)
        : m_sets(sets), m_ways(ways), m_lineBytes(lineBytes), m_writeAllocate(writeAllocate),
          m_lines(sets, std::vector<Way>(ways))
    {
    }

    Outcome access(uint64_t address, bool isWrite)
    {
        Outcome out;
        const uint64_t line = address / m_lineBytes;
        out.set = line % m_sets;
        out.tag = line / m_sets;
        std::vector<Way> &set = m_lines[out.set];
        ++m_clock;

        for (Way &way : set)
            if (way.valid && way.tag == out.tag)
            {
                way.lastUse = m_clock;
                out.hit     = true;
                ++hits;
                return out;
            }

        ++misses;
        if (isWrite && !m_writeAllocate)
            return out;

        Way *target = nullptr;
        for (Way &way : set)
            if (!way.valid)
            {
                target = &way;
                break;
            }
        if (!target)
        {
            target = &set[0];
            for (Way &way : set)
                if (way.lastUse < target->lastUse)
                    target = &way;
            out.evicted    = true;
            out.evictedTag = target->tag;
            ++evictions;
        }
        *target = {true, out.tag, m_clock};
        return out;
    }

    const Way &way(uint64_t set, uint64_t way) const { return m_lines[set][way]; }

    uint64_t hits{0}, misses{0}, evictions{0};

  private:
    uint64_t m_sets, m_ways, m_lineBytes;
    bool     m_writeAllocate;
    std::vector<std::vector<Way>> m_lines;
    uint64_t m_clock{0};
};
} // namespace Atlas::test_support

#endif // ATLAS_TEST_CACHE_REFERENCE_MODEL_H
