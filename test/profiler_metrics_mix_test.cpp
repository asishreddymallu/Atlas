/**
 * @file profiler_metrics_mix_test.cpp
 * @brief Metric 1, the instruction mix: the calculator, the retirement
 *        stream, the agreement with the existing type table, hand-computed
 *        counts, Undo, and the panel in the real window.
 */
#include "profiler_metrics_test_support.h"

#include "processor/processor_manager.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/timing_grid/timing_grid.h"

#include <QApplication>
#include <QDirIterator>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>

#include <gtest/gtest.h>

#include <numeric>

using namespace Atlas;
using namespace Atlas::test_support::metrics;
using IT = instruction_set::InstructionType;

namespace
{
TraceCycle retire(uint32_t cycle, uint64_t pc, uint32_t word, uint32_t id)
{
    TraceCycle r;
    r.cycle       = cycle;
    r.singleCycle = true;
    r.occ[0]      = TraceOccupant{pc, word, id};
    return r;
}

constexpr uint32_t kAddi = 0x00100093; // addi x1, x0, 1
constexpr uint32_t kBeq  = 0x00000463; // beq x0, x0, +8

/// Every example program that ends without input, as test-relative paths.
std::vector<std::string> examplePrograms()
{
    std::vector<std::string> out;
    QDirIterator it(QStringLiteral("../examples"), {QStringLiteral("*.s"), QStringLiteral("*.asm")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        // A self-loop by design (known_issues 134): it never ends.
        if (path.endsWith(QStringLiteral("cache_test_simple.asm")))
            continue;
        out.push_back(path.toStdString());
    }
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

// ---------------------------------------------------------------- calculator

TEST(ProfilerMetricsMix, SharesAreAbsentWhenNothingRetired)
{
    EXPECT_FALSE(shareOf(0, 0).has_value());
    EXPECT_FALSE(shareOf(3, 0).has_value());
    EXPECT_DOUBLE_EQ(*shareOf(1, 4), 0.25);
    EXPECT_EQ(MoreMetricsPanel::percent(std::nullopt), QStringLiteral("n/a"));
    EXPECT_EQ(MoreMetricsPanel::percent(0.125), QStringLiteral("12.50 %"));
}

TEST(ProfilerMetricsMix, MnemonicsAreSortedByCountThenName)
{
    MetricsProgram p;
    p.mnemonic = {"sw", "addi", "lw", "add"};
    p.line     = {1, 2, 3, 4};
    p.word     = {0, 0, 0, 0};
    const InstructionMix mix = computeInstructionMix({{0, 2}, {4, 5}, {8, 2}, {12, 5}}, p);
    ASSERT_EQ(mix.byMnemonic.size(), 4u);
    EXPECT_EQ(mix.byMnemonic[0].mnemonic, "add"); // 5, and "add" < "addi"
    EXPECT_EQ(mix.byMnemonic[1].mnemonic, "addi");
    EXPECT_EQ(mix.byMnemonic[2].mnemonic, "lw"); // 2, and "lw" < "sw"
    EXPECT_EQ(mix.byMnemonic[3].mnemonic, "sw");
    EXPECT_EQ(mix.retired, 14u);
    EXPECT_EQ(mix.loads, 2u);
    EXPECT_EQ(mix.stores, 2u);
    EXPECT_EQ(mix.byType[toIndex(IT::R_TYPE)], 5u);
    EXPECT_EQ(mix.byType[toIndex(IT::I_TYPE)], 7u); // addi + lw
    EXPECT_EQ(mix.byType[toIndex(IT::S_TYPE)], 2u);
}

TEST(ProfilerMetricsMix, LoadAndStoreListsAreExactlyTheDefinedOnes)
{
    for (const char *m : {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "flw", "fld"})
        EXPECT_TRUE(isLoadMnemonic(m)) << m;
    for (const char *m : {"sb", "sh", "sw", "sd", "fsw", "fsd"})
        EXPECT_TRUE(isStoreMnemonic(m)) << m;
    for (const char *m : {"addi", "lui", "jalr", "la", "li"})
    {
        EXPECT_FALSE(isLoadMnemonic(m)) << m;
        EXPECT_FALSE(isStoreMnemonic(m)) << m;
    }
}

// ---------------------------------------------------------------- the stream

TEST(ProfilerMetricsStream, CountsRetiresAndRollsBackOnUndo)
{
    RetirementStream s;
    s.feed(retire(1, 0, kAddi, 1));
    s.feed(retire(2, 4, kBeq, 2));
    s.feed(retire(3, 12, kAddi, 3)); // not 4 + 4: the beq was taken
    EXPECT_EQ(s.retired(), 3u);
    EXPECT_EQ(s.branches(), 1u);
    EXPECT_EQ(s.branchesTaken(), 1u);

    s.rollbackTo(2); // Undo one step: the branch is unresolved again
    EXPECT_EQ(s.retired(), 2u);
    EXPECT_EQ(s.branchesTaken(), 0u);
    EXPECT_EQ(s.branchesResolved(), 0u);
    s.feed(retire(3, 8, kAddi, 4)); // a different future: not taken
    EXPECT_EQ(s.branchesTaken(), 0u);
    EXPECT_EQ(s.branchesResolved(), 1u);
    EXPECT_EQ(s.retiredByPc().count(12), 0u);
    EXPECT_EQ(s.retiredByPc().at(8), 1u);
    EXPECT_TRUE(s.complete());
}

TEST(ProfilerMetricsStream, ASeenCycleArrivingAgainIsUndoThenRedo)
{
    RetirementStream s;
    s.feed(retire(1, 0, kAddi, 1));
    s.feed(retire(2, 4, kAddi, 2));
    s.feed(retire(2, 4, kAddi, 2));
    EXPECT_EQ(s.retired(), 2u);
    EXPECT_EQ(s.retiredByPc().at(4), 1u);
}

TEST(ProfilerMetricsStream, AMissedCycleMakesItIncomplete)
{
    RetirementStream s;
    s.feed(retire(1, 0, kAddi, 1));
    s.feed(retire(3, 8, kAddi, 3));
    EXPECT_FALSE(s.complete());
    s.reset();
    EXPECT_TRUE(s.complete());
    EXPECT_EQ(s.retired(), 0u);
}

TEST(ProfilerMetricsStream, BubblesInWbRetireNothing)
{
    TraceCycle r;
    r.cycle = 1; // five-stage, WB empty
    RetirementStream s;
    s.feed(r);
    EXPECT_EQ(s.retired(), 0u);
    EXPECT_EQ(s.lastCycle(), 1u);
}

// ---------------------------------------------------------------- programs

TEST(ProfilerMetricsMix, TheMachineMnemonicTableMatchesTheTextBuffer)
{
    // The mix reads one intermediate unit per machine word.
    for (const std::string &path : examplePrograms())
    {
        const AssembledProgram program = assemble(path);
        EXPECT_EQ(program.intermediate_code.size(), program.text_buffer.size()) << path;
    }
}

TEST(ProfilerMetricsMix, LoadUseOriginalByHand)
{
    // addi, addi, sw, lw, add, addi - on every model.
    for (const auto &[name, type] : allModels())
    {
        const MetricsRun     run = runFile("../examples/scheduler/load_use_original.s", type);
        const InstructionMix mix = run.mix();
        ASSERT_TRUE(run.halted) << name;
        EXPECT_EQ(mix.retired, 6u) << name;
        EXPECT_EQ(countOf(mix, "addi"), 3u) << name;
        EXPECT_EQ(countOf(mix, "sw"), 1u) << name;
        EXPECT_EQ(countOf(mix, "lw"), 1u) << name;
        EXPECT_EQ(countOf(mix, "add"), 1u) << name;
        EXPECT_EQ(mix.loads, 1u) << name;
        EXPECT_EQ(mix.stores, 1u) << name;
        EXPECT_EQ(typeCount(mix, IT::I_TYPE), 4u) << name; // 3 addi + lw
        EXPECT_EQ(typeCount(mix, IT::R_TYPE), 1u) << name;
        EXPECT_EQ(typeCount(mix, IT::S_TYPE), 1u) << name;
    }
}

TEST(ProfilerMetricsMix, Stride1ByHand)
{
    // 9 prologue + 3 outer + 1024 x 10 loop + 4 tail = 10,256 retired:
    // addi 7 + 3 + 1024 x 7 + 3 = 7,181; lw, add, blt 1,024 each; lui 2; bne 1.
    const MetricsRun     run = runFile("../examples/cache_lab/stride_1.s", ProcessorType::RV5Stage_H_F);
    const InstructionMix mix = run.mix();
    ASSERT_TRUE(run.halted);
    EXPECT_EQ(mix.retired, 10256u);
    EXPECT_EQ(countOf(mix, "addi"), 7181u);
    EXPECT_EQ(countOf(mix, "lw"), 1024u);
    EXPECT_EQ(countOf(mix, "add"), 1024u);
    EXPECT_EQ(countOf(mix, "blt"), 1024u);
    EXPECT_EQ(countOf(mix, "lui"), 2u);
    EXPECT_EQ(countOf(mix, "bne"), 1u);
    EXPECT_EQ(mix.loads, 1024u);
    EXPECT_EQ(mix.stores, 0u);
    EXPECT_EQ(typeCount(mix, IT::B_TYPE), 1025u);
    EXPECT_EQ(typeCount(mix, IT::U_TYPE), 2u);
    ASSERT_FALSE(mix.byMnemonic.empty());
    EXPECT_EQ(mix.byMnemonic.front().mnemonic, "addi");
}

TEST(ProfilerMetricsMix, OnEveryExampleAndModelTheMixAgreesWithTheExistingTable)
{
    // Per-mnemonic counts sum to the processor's retired count, per-type
    // counts equal the existing instruction-type table, shares sum to 100 %.
    int checked = 0;
    for (const std::string &path : examplePrograms())
        for (const auto &[name, type] : allModels())
        {
            MetricsRun run;
            try
            {
                run = runFile(path, type, 40000); // every halting example ends within 15,117 cycles
            }
            catch (const std::exception &)
            {
                continue; // a program this model cannot assemble or run
            }
            const InstructionMix mix = run.mix();
            const uint64_t       sum = std::accumulate(
                mix.byMnemonic.begin(), mix.byMnemonic.end(), uint64_t{0},
                [](uint64_t a, const MnemonicCount &m) { return a + m.count; });
            const std::string where = path + " on " + name;
            EXPECT_EQ(sum, mix.retired) << where;
            EXPECT_EQ(mix.retired, run.processor->instructions_retired_) << where;
            EXPECT_EQ(mix.byType, run.existingTypeTable()) << where;
            if (mix.retired > 0)
            {
                double shares = 0;
                for (uint64_t c : mix.byType)
                    shares += *shareOf(c, mix.retired);
                EXPECT_NEAR(shares, 1.0, 1e-9) << where;
            }
            ++checked;
        }
    EXPECT_GT(checked, 200);
}

TEST(ProfilerMetricsMix, AMixedTypePseudoLineIsCountedPerMachineInstruction)
{
    // Observation, not a change: `la` expands to auipc (U) + addi (I). The
    // existing table files the whole line under its last instruction's type
    // (I, twice); the mix counts the machine instructions (one U, one I).
    const MetricsRun     run = runSource(".data\nw: .word 7\n.text\nla x5, w\nlw x6, 0(x5)\n",
                                         ProcessorType::RVSS);
    const InstructionMix mix = run.mix();
    EXPECT_EQ(countOf(mix, "auipc"), 1u);
    EXPECT_EQ(countOf(mix, "addi"), 1u);
    EXPECT_EQ(typeCount(mix, IT::U_TYPE), 1u);
    EXPECT_EQ(typeCount(mix, IT::I_TYPE), 2u); // addi + lw
    const auto table = run.existingTypeTable();
    EXPECT_EQ(table[toIndex(IT::U_TYPE)], 0u);
    EXPECT_EQ(table[toIndex(IT::I_TYPE)], 3u);
    EXPECT_EQ(mix.retired, 3u);
}

// ---------------------------------------------------------------- the window

TEST(ProfilerMetricsMix, ThePanelSitsBelowTheExistingTablesAndShowsTheMix)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    auto *tab   = window.findChild<ProfilerTab *>();
    ASSERT_NE(tab, nullptr);
    MoreMetricsPanel *panel = tab->moreMetrics();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->title(), QStringLiteral("More metrics"));

    // Below both existing tables, in the same column.
    auto *counters = window.findChild<QTableView *>(QStringLiteral("profilerMetricsTableView"));
    auto *types    = window.findChild<QTableView *>(QStringLiteral("instructionTypeCountTableView"));
    ASSERT_NE(counters, nullptr);
    ASSERT_NE(types, nullptr);
    window.setCurrentTab(MainWindow::ProfilerTabIndex);
    QApplication::processEvents();
    EXPECT_GT(panel->mapTo(&window, QPoint(0, 0)).y(), types->mapTo(&window, QPoint(0, 0)).y());
    EXPECT_GT(types->mapTo(&window, QPoint(0, 0)).y(), counters->mapTo(&window, QPoint(0, 0)).y());

    // Before any run: no shares, only n/a.
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Loads share")), QStringLiteral("n/a"));
    EXPECT_EQ(panel->summarySource(QStringLiteral("Loads share")), QStringLiteral("[Modelled]"));
    EXPECT_EQ(panel->summarySource(QStringLiteral("Loads retired")), QStringLiteral("[Measured]"));

    // A run through the window's own manager: the panel shows the mix.
    auto *grid = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"));
    ASSERT_NE(grid, nullptr);
    ProcessorManager *manager = grid->manager();
    window.processorChanged(ProcessorType::RV5Stage_H_F);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/scheduler/load_use_original.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->run();
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Instructions retired (mix total)")), QStringLiteral("6"));
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Loads share")), QStringLiteral("16.67 %"));
    QStandardItemModel *m = panel->mnemonicModel();
    ASSERT_GE(m->rowCount(), 4);
    EXPECT_EQ(m->item(0, 0)->text(), QStringLiteral("addi"));
    EXPECT_EQ(m->item(0, 1)->text(), QStringLiteral("3"));
    EXPECT_EQ(m->item(0, 2)->text(), QStringLiteral("50.00 %"));
}
