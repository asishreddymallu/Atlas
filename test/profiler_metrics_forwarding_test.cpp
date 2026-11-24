/**
 * @file profiler_metrics_forwarding_test.cpp
 * @brief Metric 4, forwarding events: hand-counted programs, equality with
 *        the trace's own forwarded-operand records, counting past the
 *        trace's 100,000-cycle ring, and n/a on the models without forwarding.
 */
#include "profiler_metrics_test_support.h"

#include "processor/processor_manager.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support::metrics;

namespace
{
struct Expected
{
    const char *program;
    uint64_t    exMem, memWb;
};

void expectForwards(ProcessorType type, const Expected &e)
{
    const MetricsRun run = runFile(std::string("../examples/") + e.program, type);
    ASSERT_TRUE(run.halted) << e.program;
    EXPECT_EQ(run.stream.forwardsFromExMem(), e.exMem) << e.program;
    EXPECT_EQ(run.stream.forwardsFromMemWb(), e.memWb) << e.program;
}
} // namespace

TEST(ProfilerMetricsForwarding, HandCountedOnHf)
{
    // load_use_original: sw takes x2 from EX/MEM and x1 from MEM/WB; lw reads
    // x1 from the register file; add takes x3 from MEM/WB after the load-use
    // stall.
    expectForwards(ProcessorType::RV5Stage_H_F, {"scheduler/load_use_original.s", 1, 2});
    // ex_mem_fwd: add takes t0 twice from EX/MEM; sub takes t1 from EX/MEM
    // and t0 from MEM/WB.
    expectForwards(ProcessorType::RV5Stage_H_F, {"ex_mem_fwd.s", 3, 1});
    // mem_wb_fwd: add takes t0 twice from EX/MEM; after the nop, sub takes t1
    // from MEM/WB and reads t0 from the register file.
    expectForwards(ProcessorType::RV5Stage_H_F, {"mem_wb_fwd.s", 2, 1});
}

TEST(ProfilerMetricsForwarding, HandCountedOnNhf)
{
    // No hazard unit: add follows lw with no stall and takes x3 from EX/MEM
    // (the load's address, known_issues 156) - so 2 from EX/MEM, 1 from MEM/WB.
    expectForwards(ProcessorType::RV5Stage_NH_F, {"scheduler/load_use_original.s", 2, 1});
    expectForwards(ProcessorType::RV5Stage_NH_F, {"ex_mem_fwd.s", 3, 1});
    expectForwards(ProcessorType::RV5Stage_NH_F, {"mem_wb_fwd.s", 2, 1});
}

TEST(ProfilerMetricsForwarding, EqualsTheTracesOwnForwardRecordsOnEveryModel)
{
    for (const char *program : {"scheduler/load_use_original.s", "ex_mem_fwd.s", "mem_wb_fwd.s",
                                "cache_lab/stride_16.s", "R_R_hazard.s", "L_U_hazard.s"})
        for (const auto &[name, type] : allModels())
        {
            const MetricsRun run = runFile(std::string("../examples/") + program, type);
            uint64_t exMem = 0, memWb = 0;
            for (const TraceCycle &record : run.processor->Trace().all())
                for (const TraceForward &f : record.fwd)
                    (f.from == TraceStage::MEM ? exMem : memWb)++;
            EXPECT_EQ(run.stream.forwardsFromExMem(), exMem) << program << " on " << name;
            EXPECT_EQ(run.stream.forwardsFromMemWb(), memWb) << program << " on " << name;
            if (type != ProcessorType::RV5Stage_NH_F && type != ProcessorType::RV5Stage_H_F)
                EXPECT_EQ(exMem + memWb, 0u) << program << " on " << name;
        }
}

TEST(ProfilerMetricsForwarding, KeepsCountingPastTheTracesRing)
{
    // li x6, N (lui + addi for these N); L: addi x5, x5, 1; bne x5, x6, L.
    // Forwards: the li's addi takes x6 from the lui (EX/MEM); per pass, bne
    // takes x5 from EX/MEM; the first bne takes x6 from MEM/WB. So N + 1 from
    // EX/MEM and 1 from MEM/WB. On h-f a pass is 5 cycles (two instructions
    // and a taken branch's 3).
    const auto program = [](int n)
    {
        return "li x6, " + std::to_string(n) + "\nL:\naddi x5, x5, 1\nbne x5, x6, L\n";
    };
    // N = 3,000: about 15,000 cycles, inside the ring, so the formula is
    // checked against the trace's own records first.
    const MetricsRun shortRun = runSource(program(3000), ProcessorType::RV5Stage_H_F);
    uint64_t exMem = 0, memWb = 0;
    for (const TraceCycle &record : shortRun.processor->Trace().all())
        for (const TraceForward &f : record.fwd)
            (f.from == TraceStage::MEM ? exMem : memWb)++;
    EXPECT_EQ(shortRun.processor->Trace().dropped(), 0u);
    EXPECT_EQ(exMem, 3001u);
    EXPECT_EQ(memWb, 1u);
    EXPECT_EQ(shortRun.stream.forwardsFromExMem(), 3001u);
    EXPECT_EQ(shortRun.stream.forwardsFromMemWb(), 1u);

    // N = 30,000: about 150,000 cycles, past the 100,000 the ring keeps.
    const MetricsRun longRun = runSource(program(30000), ProcessorType::RV5Stage_H_F);
    ASSERT_TRUE(longRun.halted);
    EXPECT_GT(longRun.processor->Trace().dropped(), 0u) << "the ring must have overflowed";
    EXPECT_TRUE(longRun.stream.complete());
    EXPECT_EQ(longRun.stream.forwardsFromExMem(), 30001u);
    EXPECT_EQ(longRun.stream.forwardsFromMemWb(), 1u);
    EXPECT_EQ(longRun.stream.retired(), 2u + 2u * 30000u);
}

TEST(ProfilerMetricsForwarding, ThePanelShowsNaWithoutAForwardingUnit)
{
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    for (const auto &[name, type] : allModels())
    {
        window.processorChanged(type);
        setupVmStateDirectory();
        manager->setStepDelay(0);
        manager->loadProgram(assemble("../examples/ex_mem_fwd.s"));
        manager->setBreakpoints({}); // known_issues 86
        manager->run();
        panel->refresh();
        const bool forwarding = type == ProcessorType::RV5Stage_NH_F || type == ProcessorType::RV5Stage_H_F;
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Forwarded operands")),
                  forwarding ? QStringLiteral("4") : QStringLiteral("n/a"))
            << name;
        EXPECT_EQ(panel->summaryValue(QStringLiteral("Forwarded from EX/MEM")),
                  forwarding ? QStringLiteral("3") : QStringLiteral("n/a"))
            << name;
        EXPECT_EQ(panel->summarySource(QStringLiteral("Forwarded operands")), QStringLiteral("[Measured]"));
    }
}

TEST(ProfilerMetricsForwarding, TheWindowsCollectorAlsoCountsPastTheRing)
{
    // The same program through the real window: the collector reads each
    // cycle on the clock signal, so the ring's limit does not stop it.
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    window.processorChanged(ProcessorType::RV5Stage_H_F);
    setupVmStateDirectory();
    const MetricsRun probe = runSource("li x6, 21000\nL:\naddi x5, x5, 1\nbne x5, x6, L\n",
                                       ProcessorType::RV5Stage_H_F, 1);
    manager->setStepDelay(0);
    manager->loadProgram(probe.program);
    manager->setBreakpoints({}); // known_issues 86
    manager->run();
    ASSERT_GT(manager->trace().dropped(), 0u) << "the ring must have overflowed";
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Forwarded from EX/MEM")), QStringLiteral("21001"));
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Forwarded from MEM/WB")), QStringLiteral("1"));
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Instructions retired (mix total)")), QStringLiteral("42002"));
}
