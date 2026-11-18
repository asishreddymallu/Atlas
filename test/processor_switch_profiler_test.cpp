/**
 * @file processor_switch_profiler_test.cpp
 * @brief Switching processor must not silently unplug the profiler.
 *
 * `ProcessorManager::changeProcessor` destroys the current processor and
 * builds a new one (`processor_manager.cpp:56-79`). `MainWindow` knows
 * that and re-points the register container, the memory tab and the cache
 * tab afterwards (`main_window.cpp:927-937`) - but not the profiler tab,
 * which is not in that list. So: does the profiler still get told about
 * clocked cycles after a switch?
 *
 * Reading the code says yes, because the profiler is a by-value member of
 * the manager (`processor_manager.h:91`) and the connection that feeds it
 * is `ProcessorManager::processorClockedSignal ->
 * Profiler::processorClockedSlot`, made once in the constructor
 * (`processor_manager.cpp:52-53`) and never touched by `changeProcessor`;
 * what `changeProcessor` re-makes is the inner hop, processor ->
 * manager (`:72-77`). This asserts it instead of believing it, for every
 * model.
 *
 * What it does NOT use is `Profiler::getLineExecutionCount()`. That
 * always returns 0: `Profiler::m_lineNumberToExecutionCounts` is only
 * ever cleared and read (`profiler.cpp:15, 47-48`), never written - the
 * counting happens in `ProfilerEditor::m_lineNumberToExecutionCounts`
 * (`profiler_editor.cpp:113`), on the far side of the signal. The
 * emission is therefore what "the profiler received a count" means here.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "profiler/profiler.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QObject>
#include <QTemporaryDir>

#include <fstream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
/// Three independent `addi`s. No dependent pair, so it means the same on
/// all five models, and no branch, so `Run()` terminates even on the
/// variant with neither hazard detection nor forwarding.
const char *kProgram = ".text\n"
                       "    addi t0, x0, 1\n"
                       "    addi t1, x0, 2\n"
                       "    addi t2, x0, 3\n";

int serial = 0;

AssembledProgram programOnDisk(const QTemporaryDir &dir)
{
    const std::string path = dir.path().toStdString() + "/sw" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << kProgram;
    }
    setupVmStateDirectory();
    return assemble(path);
}

std::string nameOf(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RVSS:           return "RVSS";
    case ProcessorType::RV5Stage_NH_NF: return "RV5Stage_NH_NF";
    case ProcessorType::RV5Stage_H_NF:  return "RV5Stage_H_NF";
    case ProcessorType::RV5Stage_NH_F:  return "RV5Stage_NH_F";
    case ProcessorType::RV5Stage_H_F:   return "RV5Stage_H_F";
    default:                            return "?";
    }
}

/// Runs the program once and returns how many line-count increments the
/// profiler was handed. `switchTo == nullptr` means "do not switch".
int countsAfter(const QTemporaryDir &dir, const ProcessorType *switchTo)
{
    ProcessorManager manager;
    int              fired = 0;

    QObject::connect(manager.getProfiler(), &Profiler::incrementLineExecutionCountSignal, &manager,
                     [&fired](int) { ++fired; });

    if (switchTo)
        manager.changeProcessor(*switchTo);

    // `Run()` waits step_delay_ ms after every step
    // (`rv5s_processor_base.cpp:46`) and the default is 1000
    // (`processor_base.h:178`). changeProcessor re-applies the manager's
    // remembered delay, so this is set after the switch.
    manager.setStepDelay(0);
    manager.loadProgram(programOnDisk(dir));
    manager.run();
    return fired;
}
} // namespace

TEST(ProcessorSwitchProfiler, ItCountsOnTheProcessorTheWindowStartsWith)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_GT(countsAfter(dir, nullptr), 0)
        << "the profiler was not fed even without a processor change";
}

TEST(ProcessorSwitchProfiler, ItStillCountsAfterEveryProcessorChange)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (ProcessorType type : {ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF,
                               ProcessorType::RV5Stage_H_NF, ProcessorType::RV5Stage_NH_F,
                               ProcessorType::RV5Stage_H_F})
    {
        EXPECT_GT(countsAfter(dir, &type), 0)
            << "after switching to " << nameOf(type)
            << " the profiler stopped receiving execution counts";
    }
}
