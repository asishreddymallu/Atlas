/**
 * @file metrics_collector.h
 * @brief Feeds the Profiler's extra metrics from the execution trace.
 *
 * Connected with Qt::DirectConnection to ProcessorManager's clock signal,
 * like the Profiler itself: on every clock, on whichever thread made it, it
 * reads the newest trace record(s) and feeds a RetirementStream. So it sees
 * every cycle of a run, not only the trace's 100,000-cycle ring. An Undo
 * (the trace shrinks) rolls the stream back. Everything is read-only.
 */
#pragma once

#include "profiler_metrics/instruction_mix.h"
#include "profiler_metrics/retirement_stream.h"
#include "processor/processor_types.h"

#include <QObject>

#include <mutex>
#include <optional>

namespace Atlas
{
class ProcessorManager;

class MetricsCollector : public QObject
{
    Q_OBJECT
  public:
    struct Snapshot
    {
        bool           complete{true};
        ProcessorType  processor{ProcessorType::RVSS};
        InstructionMix mix;
        uint64_t       branches{0};
        uint64_t       branchesResolved{0};
        uint64_t       taken{0};
        uint64_t       forwardsExMem{0};
        uint64_t       forwardsMemWb{0};
        std::optional<unsigned int> mispredictions; ///< the model's own counter, if it has one
        bool           programEnded{false};
        unsigned int   cycles{0};
        uint32_t       lastCycle{0}; ///< the last trace cycle the stream has seen
    };

    explicit MetricsCollector(ProcessorManager *manager, QObject *parent = nullptr);

    /// Thread-safe copy of the current numbers.
    Snapshot snapshot() const;
    const MetricsProgram &program() const { return m_program; }
    /// The stream's retired counts by address, under the lock.
    std::map<uint64_t, uint64_t> retiredByPc() const;

  public slots:
    /// Reads the trace up to its newest record (any thread).
    void sync();
    /// A new program or processor: start again.
    void reset();

  private:
    ProcessorManager  *m_manager{nullptr};
    mutable std::mutex m_mutex;
    RetirementStream   m_stream;
    MetricsProgram     m_program;
};
} // namespace Atlas
