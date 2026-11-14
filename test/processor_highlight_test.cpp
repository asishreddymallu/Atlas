/**
 * @file processor_highlight_test.cpp
 * @brief The clocked-state highlight must survive a state with no PCs.
 *
 * `ProcessorManager::processorClockedSlot` is a public slot, so the crash
 * from issue 19 is reproducible here directly, with no window and no
 * resources: `RVSSProcessor::Run()` emits one last clocked signal after its
 * loop, and on a Debug Run that emit can be reached before anything has
 * been clocked, so `ProcessorState::programCounters` is still the empty
 * vector it was default-constructed as.
 *
 * Without the guard this does not fail, it ABORTS - `operator[]` on an
 * empty vector trips libstdc++'s assertion, which this toolchain enables by
 * default, and segmentation-faults without it, because `data()` is null.
 */
#include "processor/processor_manager.h"
#include "processor/processor_state.h"

#include <gtest/gtest.h>

#include <QObject>

using namespace Kites;

TEST(ProcessorHighlight, AClockedStateWithNoProgramCountersIsNotSubscripted)
{
    ProcessorManager manager;

    // QSignalSpy is not linked; a lambda counter does the same job.
    int fired = 0;
    QObject::connect(&manager, &ProcessorManager::updateEditorHighlightSignal, &manager,
                     [&fired](const std::vector<std::pair<int, std::string>> &,
                              const std::vector<std::pair<int, std::string>> &)
                     { ++fired; });

    ProcessorState empty; // default-constructed: programCounters is {}
    ASSERT_TRUE(empty.programCounters.empty());
    manager.processorClockedSlot(empty);

    EXPECT_EQ(fired, 0) << "an empty state should produce no highlight at all";
}

TEST(ProcessorHighlight, ARealClockedStateStillProducesAHighlight)
{
    // The guard must skip nothing else: a state that does carry a program
    // counter still reaches the signal.
    ProcessorManager manager;

    int fired = 0;
    QObject::connect(&manager, &ProcessorManager::updateEditorHighlightSignal, &manager,
                     [&fired](const std::vector<std::pair<int, std::string>> &,
                              const std::vector<std::pair<int, std::string>> &)
                     { ++fired; });

    ProcessorState state;
    state.programCounters = {0, 0, 0, 0, 0};
    state.lastExecutedPC  = 0;
    manager.processorClockedSlot(state);

    EXPECT_EQ(fired, 1) << "a populated state no longer reaches the highlight";
}
