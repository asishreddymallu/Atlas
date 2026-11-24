#include "ui/profiler_tab/metrics_collector.h"

#include "processor/processor_manager.h"
#include "profiler/profiler.h"

namespace Atlas
{
MetricsCollector::MetricsCollector(ProcessorManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    // Direct: the clock arrives on the worker thread during a Run, and every
    // cycle must be read before the ring could drop it.
    connect(manager, &ProcessorManager::processorClockedSignal, this, &MetricsCollector::sync,
            Qt::DirectConnection);
    connect(manager, &ProcessorManager::processorStateChangedSignal, this, &MetricsCollector::sync,
            Qt::DirectConnection);
    if (const Profiler *profiler = manager->getProfiler())
        connect(profiler, &Profiler::profilerReset, this, &MetricsCollector::reset,
                Qt::DirectConnection);
    reset();
}

void MetricsCollector::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stream.reset();
    m_program = MetricsProgram::from(m_manager->currentProgram());
}

void MetricsCollector::sync()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const ExecutionTrace &trace = m_manager->trace();
    TraceCycle            newest;
    if (!trace.last(newest))
    {
        if (m_stream.lastCycle() != 0)
            m_stream.reset(); // the trace was cleared: a reset or a new processor
        return;
    }
    const uint32_t seen = m_stream.lastCycle();
    if (newest.cycle < seen)
    {
        m_stream.rollbackTo(newest.cycle); // Undo
        return;
    }
    if (newest.cycle == seen)
        return;
    const uint32_t from = seen + 1;
    if (trace.firstCycle() > from)
    {
        // The ring no longer holds what was missed. Keep counting what is
        // there, but say the totals are incomplete.
        m_stream.markGap();
        for (const TraceCycle &record : trace.window(trace.firstCycle(), newest.cycle - trace.firstCycle() + 1))
            m_stream.feed(record);
        return;
    }
    if (newest.cycle == from)
    {
        m_stream.feed(newest);
        return;
    }
    for (const TraceCycle &record : trace.window(from, newest.cycle - seen))
        m_stream.feed(record);
}

MetricsCollector::Snapshot MetricsCollector::snapshot() const
{
    Snapshot s;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        s.complete         = m_stream.complete();
        s.lastCycle        = m_stream.lastCycle();
        s.mix              = computeInstructionMix(m_stream.retiredByPc(), m_program);
        s.branches         = m_stream.branches();
        s.branchesResolved = m_stream.branchesResolved();
        s.taken            = m_stream.branchesTaken();
        s.forwardsExMem    = m_stream.forwardsFromExMem();
        s.forwardsMemWb    = m_stream.forwardsFromMemWb();
    }
    s.processor      = m_manager->getProcessorType();
    s.mispredictions = m_manager->metrics().mispredictions;
    s.programEnded   = m_manager->programEnded();
    s.cycles         = m_manager->getCycles();
    return s;
}

std::map<uint64_t, uint64_t> MetricsCollector::retiredByPc() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stream.retiredByPc();
}
} // namespace Atlas
