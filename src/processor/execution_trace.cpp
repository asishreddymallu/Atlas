#include "processor/execution_trace.h"

namespace Kites
{
const char *traceStageName(TraceStage stage)
{
    switch (stage)
    {
    case TraceStage::IF: return "IF";
    case TraceStage::ID: return "ID";
    case TraceStage::EX: return "EX";
    case TraceStage::MEM: return "MEM";
    case TraceStage::WB: return "WB";
    }
    return "?";
}

const char *stallReasonName(StallReason reason)
{
    switch (reason)
    {
    case StallReason::None: return "none";
    case StallReason::LoadUse: return "load_use";
    case StallReason::RawNoForwarding: return "raw_no_forwarding";
    case StallReason::Unknown: return "unknown";
    }
    return "?";
}

const char *rawResolutionName(RawResolution resolution)
{
    switch (resolution)
    {
    case RawResolution::Stall: return "stall";
    case RawResolution::Forward: return "forward";
    case RawResolution::Unhandled: return "unhandled";
    }
    return "?";
}

const char *redirectKindName(RedirectKind kind)
{
    switch (kind)
    {
    case RedirectKind::None: return "none";
    case RedirectKind::Branch: return "branch";
    case RedirectKind::Jump: return "jump";
    case RedirectKind::Csr: return "csr";
    case RedirectKind::Ecall: return "ecall";
    }
    return "?";
}

void ExecutionTrace::push(const TraceCycle &record)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.push_back(record);
    while (m_records.size() > m_capacity)
    {
        m_records.pop_front();
        ++m_dropped;
    }
}

void ExecutionTrace::truncateAfter(uint32_t lastKept)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_records.empty() && m_records.back().cycle > lastKept)
        m_records.pop_back();
}

void ExecutionTrace::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
    m_dropped = 0;
}

size_t ExecutionTrace::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.size();
}

uint64_t ExecutionTrace::dropped() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dropped;
}

uint32_t ExecutionTrace::firstCycle() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.empty() ? 0 : m_records.front().cycle;
}

uint32_t ExecutionTrace::lastCycle() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.empty() ? 0 : m_records.back().cycle;
}

std::vector<TraceCycle> ExecutionTrace::window(uint32_t first, size_t count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TraceCycle> out;
    if (m_records.empty() || count == 0)
        return out;
    // Cycles are consecutive, so the index is the offset from the front.
    const uint32_t front = m_records.front().cycle;
    size_t         begin = first > front ? first - front : 0;
    for (size_t i = begin; i < m_records.size() && out.size() < count; ++i)
        if (m_records[i].cycle >= first)
            out.push_back(m_records[i]);
    return out;
}

std::vector<TraceCycle> ExecutionTrace::all() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_records.begin(), m_records.end()};
}

bool ExecutionTrace::last(TraceCycle &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_records.empty())
        return false;
    out = m_records.back();
    return true;
}

void addToTotals(TraceTotals &t, const TraceCycle &r, int sign)
{
    auto add = [sign](unsigned &v, unsigned n) { v = sign > 0 ? v + n : v - n; };
    if (r.stalled)
    {
        switch (r.stall.reason)
        {
        case StallReason::LoadUse: add(t.stallLoadUse, 1); break;
        case StallReason::RawNoForwarding: add(t.stallRawNoForward, 1); break;
        default: add(t.stallUnknown, 1); break;
        }
    }
    const auto n = static_cast<unsigned>(r.redirect.squashed.size());
    switch (r.redirect.kind)
    {
    case RedirectKind::Branch: add(t.flushedBranch, n); break;
    case RedirectKind::Jump: add(t.flushedJump, n); break;
    case RedirectKind::Csr:
    case RedirectKind::Ecall: add(t.flushedTrap, n); break;
    case RedirectKind::None: break;
    }
}

TraceTotals traceTotals(const std::vector<TraceCycle> &records)
{
    TraceTotals t;
    for (const TraceCycle &r : records)
        addToTotals(t, r, +1);
    return t;
}
} // namespace Kites
