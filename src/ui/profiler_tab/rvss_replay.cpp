#include "ui/profiler_tab/rvss_replay.h"

#include "processor/rvss/rvss_processor.h"

#include <QTimer>

namespace Atlas
{
RvssReplay::RvssReplay(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, [this] { step(kStepsPerChunk); });
}

RvssReplay::~RvssReplay() = default;

void RvssReplay::start(const AssembledProgram &program)
{
    cancel();
    m_processor = std::make_unique<RVSSProcessor>();
    m_processor->LoadProgram(program);
    m_state = State::Running;
    m_timer->start();
}

void RvssReplay::cancel()
{
    m_timer->stop();
    m_processor.reset();
    m_state  = State::Idle;
    m_cycles = std::nullopt;
}

void RvssReplay::runToEnd()
{
    while (m_state == State::Running)
        step(kStepsPerChunk);
}

void RvssReplay::step(unsigned maxSteps)
{
    if (m_state != State::Running || !m_processor)
        return;
    ProcessorBase &p = *m_processor;
    for (unsigned i = 0; i < maxSteps; ++i)
    {
        if (p.cycle_s_ >= kCycleCap)
        {
            m_state = State::TooLong;
            break;
        }
        if (p.NextStepWaitsForInput())
        {
            m_state = State::NeedsInput;
            break;
        }
        if (!p.StepAndNotify())
        {
            m_state  = State::Finished;
            m_cycles = p.cycle_s_;
            break;
        }
    }
    if (m_state != State::Running)
    {
        m_timer->stop();
        m_processor.reset();
        emit finishedSignal();
    }
}
} // namespace Atlas
