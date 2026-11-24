/**
 * @file profiler_metrics_branch_test.cpp
 * @brief Metric 2, branch statistics: the calculator, hand-computed counts,
 *        and the agreement with the pipelines' own counters.
 */
#include "profiler_metrics_test_support.h"

#include "profiler_metrics/branch_stats.h"
#include "processor/processor_manager.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/timing_grid/timing_grid.h"

#include <QDirIterator>

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support::metrics;

namespace
{
std::vector<std::string> haltingExamples()
{
    std::vector<std::string> out;
    QDirIterator it(QStringLiteral("../examples"), {QStringLiteral("*.s"), QStringLiteral("*.asm")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out.push_back(it.next().toStdString());
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

TEST(ProfilerMetricsBranch, RatesAreNaWithoutBranchesOrWithoutACounter)
{
    const BranchStats none = computeBranchStats(0, 0, 0);
    EXPECT_FALSE(none.takenRate.has_value());
    EXPECT_FALSE(none.mispredictionRate.has_value());
    const BranchStats rvss = computeBranchStats(4, 3, std::nullopt);
    EXPECT_DOUBLE_EQ(*rvss.takenRate, 0.75);
    EXPECT_FALSE(rvss.mispredictionRate.has_value());
    const BranchStats pipe = computeBranchStats(4, 3, 3);
    EXPECT_DOUBLE_EQ(*pipe.mispredictionRate, 0.75);
    EXPECT_EQ(MoreMetricsPanel::percent(rvss.mispredictionRate), QStringLiteral("n/a"));
}

TEST(ProfilerMetricsBranch, Stride1AndTemporalRepeatByHand)
{
    // stride_1: blt runs 1,024 times, taken all but the last; the final bne
    // falls through. temporal_repeat: 256 blt (4 sweeps of 64, each taken
    // 63 times) and 4 bne (taken 3 times): 260 branches, 255 taken.
    for (const auto &[name, type] : allModels())
    {
        const MetricsRun a = runFile("../examples/cache_lab/stride_1.s", type);
        EXPECT_EQ(a.stream.branches(), 1025u) << name;
        EXPECT_EQ(a.stream.branchesTaken(), 1023u) << name;
        const MetricsRun b = runFile("../examples/cache_lab/temporal_repeat.s", type);
        EXPECT_EQ(b.stream.branches(), 260u) << name;
        EXPECT_EQ(b.stream.branchesTaken(), 255u) << name;
    }
    const BranchStats s = computeBranchStats(1025, 1023, std::nullopt);
    EXPECT_EQ(MoreMetricsPanel::percent(s.takenRate), QStringLiteral("99.80 %"));
}

TEST(ProfilerMetricsBranch, ABranchAsTheLastInstructionStaysUnresolvedAndNotTaken)
{
    const MetricsRun run =
        runSource("addi x1, x0, 1\nbeq x0, x1, end\nend:\n", ProcessorType::RVSS);
    EXPECT_EQ(run.stream.branches(), 1u);
    EXPECT_EQ(run.stream.branchesTaken(), 0u);
}

TEST(ProfilerMetricsBranch, OnThePipelinesTheExistingCountersAgreeWithTakenBranches)
{
    // The model's misprediction counter counts taken branches, and the
    // "Flushed cycles > taken branch" row counts three squashed slots per
    // taken branch (known_issues 145). Checked at the end of every example
    // program that ends, on the four pipelines; any program where either
    // fails is listed.
    std::vector<std::string> differences;
    int                      checked = 0, inFlightChecked = 0;
    for (const std::string &path : haltingExamples())
        for (const auto &[name, type] : allModels())
        {
            if (type == ProcessorType::RVSS)
                continue;
            MetricsRun run;
            try
            {
                run = runFile(path, type, 40000);
            }
            catch (const std::exception &)
            {
                continue;
            }
            const uint64_t taken = run.stream.branchesTaken();
            const uint64_t mispredictions = run.processor->branch_mispredictions_;
            const uint64_t flushed = run.processor->TraceTotalsNow().flushedBranch;
            if (!run.halted)
            {
                // Stopped mid-run (a program that never ends, cut at the
                // cap): the pipeline counts a misprediction when the branch
                // resolves in MEM, before the next instruction retires, so
                // its counters lead by the taken branches still in flight
                // (at most two), and the flush count stays 3 per misprediction.
                EXPECT_LE(taken, mispredictions) << path << " on " << name;
                EXPECT_LE(mispredictions - taken, 2u) << path << " on " << name;
                EXPECT_EQ(flushed, 3 * mispredictions) << path << " on " << name;
                ++inFlightChecked;
                continue;
            }
            if (mispredictions != taken || flushed != 3 * taken)
                differences.push_back(path + " on " + name + ": taken " + std::to_string(taken) +
                                      ", mispredictions " + std::to_string(mispredictions) +
                                      ", flushed-by-branch cycles " + std::to_string(flushed));
            ++checked;
        }
    EXPECT_GT(checked, 150);
    EXPECT_GE(inFlightChecked, 12); // the three examples that never end, on four pipelines
    std::string listed;
    for (const std::string &d : differences)
        listed += "\n  " + d;
    EXPECT_TRUE(differences.empty()) << differences.size() << " program(s) differ:" << listed;
}

TEST(ProfilerMetricsBranch, ThePanelShowsNaOnTheSingleCycleProcessorAndARateOnAPipeline)
{
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    ASSERT_NE(panel, nullptr);
    for (const ProcessorType type : {ProcessorType::RVSS, ProcessorType::RV5Stage_H_F})
    {
        window.processorChanged(type);
        setupVmStateDirectory();
        manager->setStepDelay(0);
        manager->loadProgram(assemble("../examples/cache_lab/stride_16.s"));
        manager->setBreakpoints({}); // known_issues 86
        manager->run();
        panel->refresh();
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Branches retired")), QStringLiteral("65"));
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Taken branches")), QStringLiteral("63"));
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Taken rate")), QStringLiteral("96.92 %"));
        EXPECT_EQ(panel->summarySource(QStringLiteral("Taken rate")), QStringLiteral("[Modelled]"));
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Misprediction rate")),
                  type == ProcessorType::RVSS ? QStringLiteral("n/a") : QStringLiteral("96.92 %"));
    }
}
