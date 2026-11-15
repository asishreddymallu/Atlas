/**
 * @file reset_race_test.cpp
 * @brief Regression test for known_issues 62: Reset() on the UI thread
 *        while the processor thread is inside Run().
 *
 * This reproduces the real arrangement, not a mock: ProcessorManager is
 * moved to a worker QThread exactly as MainWindow does
 * (main_window.cpp:99-100), the run is started through a queued slot
 * invocation so it executes on the worker, and reset() is then called as
 * a plain method from this thread - which is the UI thread here.
 *
 * ON THE UNFIXED TREE THIS ABORTS THE TEST BINARY rather than failing
 * cleanly: the symptom is a cache assertion, a double free or a
 * segfault, all of which kill the process. That is unavoidable for a
 * memory-corruption bug and is why it is worth pinning - a test that
 * cannot fail safely still fails loudly. Run it alone with
 *   --gtest_filter='ResetRace.*'
 * to see it.
 */
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <iostream>
#include <string>

using namespace Kites;

namespace
{
/// The processor writes a line per step to stdout. Silence it for the
/// duration of a hammer run, or the suite output drowns.
class CoutSilencer
{
  public:
    CoutSilencer() : saved_(std::cout.rdbuf()) { std::cout.rdbuf(nullptr); }
    ~CoutSilencer() { std::cout.rdbuf(saved_); }

  private:
    std::streambuf *saved_;
};

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Long enough that the worker is still inside Run() when reset lands.
const std::string kLongProgram =
    ".text\n"
    "main:\n"
    "  li t0, 4000000\n"
    "loop:\n"
    "  addi t0, t0, -1\n"
    "  bne t0, x0, loop\n"
    "  addi t1, x0, 7\n";

struct Harness
{
    explicit Harness(ProcessorType type) : manager(nullptr, type)
    {
        setupVmStateDirectory();
        manager.setStepDelay(0); // the default is 1000 ms
        manager.moveToThread(&worker);
        worker.start();
    }
    ~Harness()
    {
        manager.stop(); // StopAndReset: waits for the worker to leave Run()
        worker.quit();
        worker.wait();
    }
    QThread worker;
    ProcessorManager manager;
};
} // namespace

/// Hammering reset() while the worker runs must not corrupt anything.
/// Pre-fix this aborts the binary, usually on the first cycle.
TEST(ResetRace, HammerResetWhileRunningIsSafe)
{
    for (ProcessorType type : {ProcessorType::RVSS, ProcessorType::RV5Stage_H_F})
    {
        Harness h(type);
        CoutSilencer quiet;
        for (int cycle = 0; cycle < 20; ++cycle)
        {
            h.manager.loadProgram(kLongProgram);
            QMetaObject::invokeMethod(&h.manager, "runSlot", Qt::QueuedConnection);
            spin(2); // let the worker get well inside Run()
            for (int i = 0; i < 100; ++i)
                h.manager.reset(); // the call that used to corrupt the heap
        }
        SUCCEED();
    }
}

/// After a Stop the processor must be in the reset state and the worker
/// idle - no step may run on top of reset state.
TEST(ResetRace, AfterStopTheProcessorIsInTheResetState)
{
    for (ProcessorType type : {ProcessorType::RVSS, ProcessorType::RV5Stage_H_F})
    {
        Harness h(type);
        CoutSilencer quiet;
        for (int cycle = 0; cycle < 10; ++cycle)
        {
            h.manager.loadProgram(kLongProgram);
            QMetaObject::invokeMethod(&h.manager, "runSlot", Qt::QueuedConnection);
            spin(3); // the run is definitely under way by now
            h.manager.stop();

            EXPECT_EQ(h.manager.getProgramCounter(), 0u)
                << "cycle " << cycle << ": pc is not the reset value after Stop";
            spin(2);
            EXPECT_EQ(h.manager.getProgramCounter(), 0u)
                << "cycle " << cycle << ": a step ran after Stop returned";
        }
    }
}
