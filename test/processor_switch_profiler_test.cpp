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
 *
 * And it drives the program with Step rather than Run. `LoadProgram`
 * plants a breakpoint at `program_size_` (`processor_base.cpp:257`), and
 * the rv5s loop keeps going past that address while the pipeline drains
 * (`rv5s_processor_base.cpp:11`), so it lands on that breakpoint, sets
 * `pause_requested_` and waits on `pause_wait_condition_` forever
 * (`:20-33`) with nobody to resume it. Measured: this test hung in that
 * wait on every rv5s model until it was rewritten to Step. RVSS does not,
 * because its loop ends at `program_counter_ < program_size_`
 * (`rvss_processor.cpp:117`) and never reaches the address.
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

/// Enough cycles for three instructions to retire on the slowest of the
/// five, with room to spare; the loop stops as soon as they have.
constexpr int kStepBudget = 32;
constexpr unsigned int kInstructions = 3;

/// Steps the program through the manager - the same call the toolbar's
/// Step button makes (`main_window.cpp:400`) - and returns how many
/// line-count increments the profiler was handed.
/// `switchTo == nullptr` means "do not switch".
int countsAfter(const QTemporaryDir &dir, const ProcessorType *switchTo)
{
    ProcessorManager manager;
    int              fired = 0;

    // Was incrementLineExecutionCountSignal, one per clock. The profiler
    // now owns the counts and reports a line's new count when it changes
    // (U5, known_issues 85), so this counts those.
    QObject::connect(manager.getProfiler(), &Profiler::lineExecutionCountChangedSignal, &manager,
                     [&fired](int, int) { ++fired; });

    if (switchTo)
        manager.changeProcessor(*switchTo);

    // changeProcessor re-applies the manager's remembered delay
    // (`processor_manager.cpp:69`), which starts at 1000 ms, so this is
    // set after the switch. Step does not wait on it, but nothing here
    // should depend on that.
    manager.setStepDelay(0);
    manager.loadProgram(programOnDisk(dir));

    for (int i = 0; i < kStepBudget && manager.getInstructionsRetired() < kInstructions; ++i)
        manager.step();

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
