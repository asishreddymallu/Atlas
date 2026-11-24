/**
 * @file rvss_replay.h
 * @brief Runs a program headless on a fresh single-cycle processor, to get
 *        its cycle count for the speed-up metric.
 *
 * The processor is stepped through ProcessorBase's public interface, the way
 * src/scheduler_gate runs programs, in chunks on the UI thread (a processor
 * builds a QGraphicsScene, so it lives there), so the window stays
 * responsive. It never touches the processor the user is running.
 */
#pragma once

#include "common/assembled_program.h"

#include <QObject>

#include <memory>
#include <optional>

class QTimer;

namespace Atlas
{
class ProcessorBase;

class RvssReplay : public QObject
{
    Q_OBJECT
  public:
    enum class State
    {
        Idle,
        Running,
        Finished,   ///< ran to the end: cycles() is set
        NeedsInput, ///< the program waits for input no one will type here
        TooLong     ///< did not end within the cap
    };

    static constexpr unsigned kCycleCap      = 1000000;
    static constexpr unsigned kStepsPerChunk = 2000;

    explicit RvssReplay(QObject *parent = nullptr);
    ~RvssReplay() override;

    /// Starts again on `program` (any earlier replay is dropped).
    void start(const AssembledProgram &program);
    void cancel();
    /// Runs to the end now, without the event loop (tests).
    void runToEnd();

    State                   state() const { return m_state; }
    std::optional<unsigned> cycles() const { return m_cycles; }

  signals:
    void finishedSignal();

  private:
    void step(unsigned maxSteps);

    std::unique_ptr<ProcessorBase> m_processor;
    QTimer                        *m_timer{nullptr};
    State                          m_state{State::Idle};
    std::optional<unsigned>        m_cycles;
};
} // namespace Atlas
