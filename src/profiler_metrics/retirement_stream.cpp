#include "profiler_metrics/retirement_stream.h"

namespace Atlas
{
void RetirementStream::reset()
{
    *this = RetirementStream{};
}

std::optional<TraceOccupant> RetirementStream::retiring(const TraceCycle &record)
{
    const TraceOccupant &who =
        record.singleCycle ? record.occ[0] : record.occ[static_cast<size_t>(TraceStage::WB)];
    if (who.bubble())
        return std::nullopt;
    return who;
}

void RetirementStream::feed(const TraceCycle &record)
{
    if (record.cycle == 0)
        return;
    if (record.cycle <= m_lastCycle)
        rollbackTo(record.cycle - 1);
    if (record.cycle > m_lastCycle + 1)
        m_complete = false; // cycles in between were never seen

    CycleLog entry;
    entry.cycle         = record.cycle;
    entry.pendingBefore = m_pendingBranch;

    if (const std::optional<TraceOccupant> who = retiring(record))
    {
        entry.retiredPc = who->pc;
        ++m_retired;
        ++m_retiredByPc[who->pc];
        if (m_pendingBranch != UINT64_MAX)
        {
            entry.resolved = true;
            entry.taken    = who->pc != m_pendingBranch + 4;
            ++m_branchesResolved;
            if (entry.taken)
                ++m_taken;
            m_pendingBranch = UINT64_MAX;
        }
        if (isBranch(who->instruction))
        {
            entry.branch = true;
            ++m_branches;
            m_pendingBranch = who->pc;
        }
    }

    for (const TraceForward &forward : record.fwd)
    {
        if (forward.from == TraceStage::MEM)
            ++entry.fwdExMem;
        else
            ++entry.fwdMemWb;
    }
    m_fwdExMem += entry.fwdExMem;
    m_fwdMemWb += entry.fwdMemWb;

    m_log.push_back(entry);
    m_lastCycle = record.cycle;
}

void RetirementStream::rollbackTo(uint32_t lastKept)
{
    while (!m_log.empty() && m_log.back().cycle > lastKept)
    {
        const CycleLog entry = m_log.back();
        m_log.pop_back();
        m_fwdExMem -= entry.fwdExMem;
        m_fwdMemWb -= entry.fwdMemWb;
        if (entry.retiredPc != UINT64_MAX)
        {
            --m_retired;
            auto it = m_retiredByPc.find(entry.retiredPc);
            if (it != m_retiredByPc.end() && --it->second == 0)
                m_retiredByPc.erase(it);
            if (entry.branch)
                --m_branches;
            if (entry.resolved)
            {
                --m_branchesResolved;
                if (entry.taken)
                    --m_taken;
            }
        }
        m_pendingBranch = entry.pendingBefore;
    }
    if (lastKept < m_lastCycle)
        m_lastCycle = lastKept;
}
} // namespace Atlas
