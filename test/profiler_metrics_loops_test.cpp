/**
 * @file profiler_metrics_loops_test.cpp
 * @brief Metric 5, hottest loops: branch decoding, hand-computed loops on the
 *        stride programs, matmul_ijk's three nested loops, a program with no
 *        loop, agreement with the Profiler's per-line counts, and the tab.
 */
#include "profiler_metrics_test_support.h"

#include "processor/processor_manager.h"
#include "profiler_metrics/hot_loops.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/timing_grid/timing_grid.h"

#include <QLabel>
#include <QStandardItemModel>

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support::metrics;

namespace
{
std::vector<HotLoop> loopsOf(const MetricsRun &run)
{
    return findHotLoops(run.info, run.stream.retiredByPc(), run.stream.retired());
}
} // namespace

TEST(ProfilerMetricsLoops, OnlyBackwardBranchesAndJalsAreLoops)
{
    // 0: top: addi; 4: beq back to 0; 8: beq forward to 16; 12: jal back to 0; 16: addi
    const AssembledProgram p = assemble(
        runSource("top:\naddi x1, x1, 1\nbeq x0, x1, top\nbeq x0, x0, done\njal x0, top\ndone:\naddi x2, x0, 1\n",
                  ProcessorType::RVSS, 1)
            .program.filename);
    const MetricsProgram info = MetricsProgram::from(p);
    ASSERT_EQ(info.word.size(), 5u);
    EXPECT_EQ(backwardTargetIndex(info.word[1], 1), std::optional<uint64_t>(0));
    EXPECT_FALSE(backwardTargetIndex(info.word[2], 2).has_value()); // forward
    EXPECT_EQ(backwardTargetIndex(info.word[3], 3), std::optional<uint64_t>(0));
    EXPECT_FALSE(backwardTargetIndex(info.word[0], 0).has_value()); // not a branch
}

TEST(ProfilerMetricsLoops, Stride1ByHand)
{
    // The inner loop ("loop:" to blt) is 10 instructions a pass, 1,024
    // passes; the outer ("outer:" to bne) runs once and holds 3 + 10,240 + 4.
    for (const auto &[name, type] : allModels())
    {
        const MetricsRun           run   = runFile("../examples/cache_lab/stride_1.s", type);
        const std::vector<HotLoop> loops = loopsOf(run);
        ASSERT_EQ(loops.size(), 2u) << name;
        EXPECT_EQ(loops[0].iterations, 1u) << name;
        EXPECT_EQ(loops[0].dynamic, 10247u) << name;
        EXPECT_EQ(loops[1].iterations, 1024u) << name;
        EXPECT_EQ(loops[1].dynamic, 10240u) << name;
        EXPECT_LT(loops[0].firstLine, loops[1].firstLine) << name;
        EXPECT_GT(loops[0].lastLine, loops[1].lastLine) << name; // outer contains inner
        EXPECT_NEAR(*loops[1].share, 10240.0 / 10256.0, 1e-12) << name;
    }
}

TEST(ProfilerMetricsLoops, TheOtherStridesHaveTheSameShape)
{
    // stride_N: 4096 / (4 N) passes of the inner loop, 10 instructions each.
    for (const auto &[file, passes] : std::vector<std::pair<const char *, uint64_t>>{
             {"stride_2.s", 512}, {"stride_4.s", 256}, {"stride_8.s", 128},
             {"stride_16.s", 64}, {"stride_32.s", 32}})
    {
        const MetricsRun run = runFile(std::string("../examples/cache_lab/") + file, ProcessorType::RV5Stage_H_F);
        const std::vector<HotLoop> loops = loopsOf(run);
        ASSERT_EQ(loops.size(), 2u) << file;
        EXPECT_EQ(loops[1].iterations, passes) << file;
        EXPECT_EQ(loops[1].dynamic, passes * 10) << file;
        EXPECT_EQ(loops[0].dynamic, passes * 10 + 7) << file;
    }
}

TEST(ProfilerMetricsLoops, MatmulIjkHasThreeNestedLoopsOutermostFirst)
{
    const MetricsRun           run   = runFile("../examples/cache_lab/matmul_ijk.s", ProcessorType::RV5Stage_H_F);
    const std::vector<HotLoop> loops = loopsOf(run);
    ASSERT_EQ(loops.size(), 3u);
    // i: 8 passes, j: 8 x 8 = 64, k: 8 x 8 x 8 = 512.
    EXPECT_EQ(loops[0].iterations, 8u);
    EXPECT_EQ(loops[1].iterations, 64u);
    EXPECT_EQ(loops[2].iterations, 512u);
    EXPECT_GT(loops[0].dynamic, loops[1].dynamic);
    EXPECT_GT(loops[1].dynamic, loops[2].dynamic);
    EXPECT_LT(loops[0].firstLine, loops[1].firstLine);
    EXPECT_LT(loops[1].firstLine, loops[2].firstLine);
    EXPECT_GE(loops[0].lastLine, loops[1].lastLine);
    EXPECT_GE(loops[1].lastLine, loops[2].lastLine);
}

TEST(ProfilerMetricsLoops, AProgramWithoutALoopHasNone)
{
    for (const auto &[name, type] : allModels())
        EXPECT_TRUE(loopsOf(runFile("../examples/scheduler/load_use_original.s", type)).empty()) << name;
}

TEST(ProfilerMetricsLoops, LineCountsEqualTheProfilersPerLineCounts)
{
    // The loops sum per-line counts; the stream's equal the Profiler's own.
    for (const char *program : {"cache_lab/stride_4.s", "cache_lab/matmul_jik.s", "cache_test.s",
                                "scheduler/load_use_original.s"})
        for (const auto &[name, type] : allModels())
        {
            const MetricsRun run = runFile(std::string("../examples/") + program, type, 40000);
            std::map<int, uint64_t> profiler;
            for (const auto &[line, count] : run.profiler->getLineExecutionCounts())
                profiler[line] = static_cast<uint64_t>(count);
            EXPECT_EQ(lineCountsFrom(run.stream.retiredByPc(), run.info), profiler) << program << " on " << name;
        }
}

TEST(ProfilerMetricsLoops, TheTabListsTheLoopsAndSaysInstructionsNotCycles)
{
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    EXPECT_TRUE(panel->notes()->text().contains(QStringLiteral("instructions executed, not cycles")));
    window.processorChanged(ProcessorType::RV5Stage_H_F);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/cache_lab/stride_16.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->run();
    panel->refresh();
    QStandardItemModel *m = panel->loopModel();
    ASSERT_EQ(m->rowCount(), 2);
    EXPECT_EQ(m->item(0, 1)->text(), QStringLiteral("1"));   // outer, once
    EXPECT_EQ(m->item(0, 2)->text(), QStringLiteral("647")); // 64 x 10 + 7
    EXPECT_EQ(m->item(1, 1)->text(), QStringLiteral("64"));
    EXPECT_EQ(m->item(1, 2)->text(), QStringLiteral("640"));
    EXPECT_EQ(m->item(1, 3)->text(), QStringLiteral("97.56 %")); // 640 / 656
    EXPECT_TRUE(m->headerData(2, Qt::Horizontal).toString().contains(QStringLiteral("[Measured]")));
    EXPECT_TRUE(m->headerData(3, Qt::Horizontal).toString().contains(QStringLiteral("[Modelled]")));
}
