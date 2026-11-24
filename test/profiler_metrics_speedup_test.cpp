/**
 * @file profiler_metrics_speedup_test.cpp
 * @brief Metric 3, speed-up over the single-cycle processor: the ratio, the
 *        headless rvss replay, its n/a cases, rvss cycles == retired on every
 *        example, and the row in the window.
 */
#include "profiler_metrics_test_support.h"

#include "processor/processor_manager.h"
#include "profiler_metrics/speedup.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/profiler_tab/rvss_replay.h"
#include "ui/timing_grid/timing_grid.h"

#include <QApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QLabel>

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support::metrics;

TEST(ProfilerMetricsSpeedup, TheRatioIsRvssOverModelAndNaWhenEitherIsMissing)
{
    EXPECT_DOUBLE_EQ(*speedupOver(6u, 11u), 6.0 / 11.0);
    EXPECT_FALSE(speedupOver(std::nullopt, 11u).has_value());
    EXPECT_FALSE(speedupOver(6u, std::nullopt).has_value());
    EXPECT_FALSE(speedupOver(6u, 0u).has_value());
    EXPECT_FALSE(speedupOver(0u, 11u).has_value());
}

TEST(ProfilerMetricsSpeedup, TheReplayCountsTheSingleCycleRun)
{
    setupVmStateDirectory();
    RvssReplay replay;
    replay.start(assemble("../examples/scheduler/load_use_original.s"));
    EXPECT_EQ(replay.state(), RvssReplay::State::Running);
    replay.runToEnd();
    EXPECT_EQ(replay.state(), RvssReplay::State::Finished);
    ASSERT_TRUE(replay.cycles().has_value());
    EXPECT_EQ(*replay.cycles(), 6u);
}

TEST(ProfilerMetricsSpeedup, AProgramThatNeedsInputCannotBeReplayed)
{
    const MetricsRun probe = runSource("addi a7, x0, 63\naddi a0, x0, 0\necall\n", ProcessorType::RVSS, 10);
    RvssReplay replay;
    replay.start(probe.program);
    replay.runToEnd();
    EXPECT_EQ(replay.state(), RvssReplay::State::NeedsInput);
    EXPECT_FALSE(replay.cycles().has_value());
}

TEST(ProfilerMetricsSpeedup, OnEveryExampleRvssCyclesEqualRetiredInstructions)
{
    // A single-cycle processor retires one instruction per cycle. Checked on
    // every example; the ones that do not end within the cap are listed
    // separately (they cannot have a speed-up).
    std::vector<std::string> differ, notEnded;
    int                      checked = 0;
    QDirIterator it(QStringLiteral("../examples"), {QStringLiteral("*.s"), QStringLiteral("*.asm")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const std::string path = it.next().toStdString();
        MetricsRun        run;
        try
        {
            run = runFile(path, ProcessorType::RVSS, 40000);
        }
        catch (const std::exception &)
        {
            continue;
        }
        if (!run.halted)
        {
            notEnded.push_back(path);
            continue;
        }
        if (run.processor->cycle_s_ != run.processor->instructions_retired_)
            differ.push_back(path + ": " + std::to_string(run.processor->cycle_s_) + " cycles, " +
                             std::to_string(run.processor->instructions_retired_) + " retired");
        ++checked;
    }
    EXPECT_GT(checked, 45);
    std::string listed;
    for (const std::string &d : differ)
        listed += "\n  " + d;
    EXPECT_TRUE(differ.empty()) << listed;
    // The three examples that never end (recorded, not hidden).
    std::sort(notEnded.begin(), notEnded.end());
    EXPECT_EQ(notEnded, (std::vector<std::string>{"../examples/cache_test_2.s",
                                                  "../examples/cache_test_simple.asm",
                                                  "../examples/custom_policy_lfu_read_1000.s"}));
}

TEST(ProfilerMetricsSpeedup, TheRowIsNaUntilTheEndThenTheRatio)
{
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    ASSERT_NE(panel, nullptr);
    const QString row = QStringLiteral("Speed-up over single-cycle");
    EXPECT_TRUE(panel->notes()->text().contains(QString::fromLatin1(kSpeedupCaveat)));

    window.processorChanged(ProcessorType::RV5Stage_H_F);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/scheduler/load_use_original.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->step();             // stepping: not finished
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(row), QStringLiteral("n/a"));
    EXPECT_EQ(panel->summarySource(row), QStringLiteral("[Modelled]"));

    manager->run();
    panel->refresh();
    QElapsedTimer clock;
    clock.start();
    while (panel->rvssReplay()->state() == RvssReplay::State::Running && clock.elapsed() < 20000)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(row), QStringLiteral("0.55x")); // 6 / 11

    window.processorChanged(ProcessorType::RVSS);
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/scheduler/load_use_original.s"));
    manager->setBreakpoints({});
    manager->run();
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(row), QStringLiteral("1.00x"));
}
