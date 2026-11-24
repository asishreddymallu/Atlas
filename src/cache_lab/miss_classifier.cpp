#include "cache_lab/miss_classifier.h"

namespace Atlas
{
void MissClassifier::configure(size_t sets, size_t ways, size_t lineBytes, bool writeAllocate)
{
    m_sets          = sets ? sets : 1;
    m_ways          = ways ? ways : 1;
    m_lineBytes     = lineBytes ? lineBytes : 1;
    m_writeAllocate = writeAllocate;
    m_lines         = m_sets * m_ways;
    reset();
}

bool MissClassifier::configuredFor(size_t sets, size_t ways, size_t lineBytes, bool writeAllocate) const
{
    return sets == m_sets && ways == m_ways && lineBytes == m_lineBytes && writeAllocate == m_writeAllocate;
}

void MissClassifier::reset()
{
    m_causes = MissCauses{};
    m_seen.clear();
    m_lru.clear();
    m_where.clear();
}

void MissClassifier::access(uint64_t address, bool isWrite, bool hit)
{
    const uint64_t block = address / m_lineBytes;
    ++m_causes.accesses;

    // The fully associative LRU model, fed the same access.
    bool fullHit = false;
    const auto it = m_where.find(block);
    if (it != m_where.end())
    {
        fullHit = true;
        m_lru.splice(m_lru.begin(), m_lru, it->second);
    }
    else if (!isWrite || m_writeAllocate)
    {
        m_lru.push_front(block);
        m_where[block] = m_lru.begin();
        if (m_lru.size() > m_lines)
        {
            m_where.erase(m_lru.back());
            m_lru.pop_back();
        }
    }

    if (!hit)
    {
        ++m_causes.misses;
        if (m_seen.count(block) == 0)
            ++m_causes.compulsory;
        else if (!fullHit)
            ++m_causes.capacity;
        else
            ++m_causes.conflict;
    }
    if (m_seen.insert(block).second)
        ++m_causes.distinctBlocks;
}
} // namespace Atlas
