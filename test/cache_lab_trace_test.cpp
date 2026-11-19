/**
 * @file cache_lab_trace_test.cpp
 * @brief I2 cross-feature check: the T1 ExecutionTrace and Profiler on the
 *        E8 cache-lab programs.
 *
 * For each of the 9 programs in examples/cache_lab, on each of the four
 * pipelines: the trace holds exactly one record per cycle and dropped none,
 * the trace's stall total equals the Profiler's Stall cycles (0 on the
 * models without a hazard unit, where the Profiler shows n/a), the stall
 * split sums to it, and the flush split sums to Flushed cycles. These are
 * the longest programs in the corpus (matmul runs 16,843 cycles), so they
 * also exercise the trace well past the hand programs.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <iostream>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

using namespace Kites;

namespace
{
struct LabTraceParam
{
    std::string   program;
    std::string   model;
    ProcessorType type;
};

void PrintTo(const LabTraceParam &p, std::ostream *os)
{
    *os << p.program << "_" << p.model;
}

std::vector<LabTraceParam> allParams()
{
    const char *programs[] = {"stride_1",  "stride_2",   "stride_4",        "stride_8",  "stride_16",
                              "stride_32", "matmul_ijk", "matmul_jik", "temporal_repeat"};
    const std::pair<const char *, ProcessorType> models[] = {{"nh_nf", ProcessorType::RV5Stage_NH_NF},
                                                             {"nh_f", ProcessorType::RV5Stage_NH_F},
                                                             {"h_nf", ProcessorType::RV5Stage_H_NF},
                                                             {"h_f", ProcessorType::RV5Stage_H_F}};
    std::vector<LabTraceParam> out;
    for (const char *p : programs)
        for (const auto &[name, type] : models)
            out.push_back({p, name, type});
    return out;
}

class CacheLabTrace : public ::testing::TestWithParam<LabTraceParam>
{
};
} // namespace

TEST_P(CacheLabTrace, TraceMatchesTheProfiler)
{
    const LabTraceParam &p = GetParam();
    const std::string    path = "../examples/cache_lab/" + p.program + ".s";

    setupVmStateDirectory();
    ProcessorManager manager;
    manager.changeProcessor(p.type);
    manager.setStepDelay(0);
    const AssembledProgram program = assemble(path);
    ASSERT_FALSE(program.text_buffer.empty()) << path;
    manager.loadProgram(program);
    manager.setBreakpoints({});
    manager.run();

    const ProcessorMetrics m = manager.metrics();
    const ExecutionTrace  &trace = manager.trace();
    ASSERT_GT(m.cycles, 0u);
    RecordProperty("cycles", static_cast<int>(m.cycles));

    // One record per cycle, none dropped.
    EXPECT_EQ(trace.dropped(), 0u);
    EXPECT_EQ(trace.size(), static_cast<size_t>(m.cycles));
    EXPECT_EQ(trace.firstCycle(), 1u);
    EXPECT_EQ(trace.lastCycle(), m.cycles);

    const TraceTotals t = traceTotals(trace.all());
    const unsigned traceStalls = t.stallLoadUse + t.stallRawNoForward + t.stallUnknown;
    std::cout << "[cycles] " << p.program << " " << p.model << " cycles=" << m.cycles << " retired=" << m.retired
              << " stalls=" << traceStalls << " (load-use " << t.stallLoadUse << ", raw " << t.stallRawNoForward
              << ") flushed=" << (m.flushed ? *m.flushed : 0u) << " (branch " << t.flushedBranch << ", jump "
              << t.flushedJump << ", trap " << t.flushedTrap << ")\n";
    EXPECT_EQ(t.stallUnknown, 0u);

    const bool hdu = p.type == ProcessorType::RV5Stage_H_NF || p.type == ProcessorType::RV5Stage_H_F;
    if (hdu)
    {
        ASSERT_TRUE(m.stalls && m.stallsLoadUse && m.stallsRawNoForwarding);
        EXPECT_EQ(traceStalls, *m.stalls) << "the trace's stall total is the Profiler's Stall cycles";
        EXPECT_EQ(*m.stallsLoadUse + *m.stallsRawNoForwarding, *m.stalls) << "the split sums to the total";
        EXPECT_EQ(*m.stallsLoadUse, t.stallLoadUse);
        EXPECT_EQ(*m.stallsRawNoForwarding, t.stallRawNoForward);
    }
    else
    {
        EXPECT_FALSE(m.stalls) << "no hazard unit: the Profiler shows n/a";
        EXPECT_EQ(traceStalls, 0u) << "no hazard unit: the trace records no stall";
    }

    ASSERT_TRUE(m.flushed && m.flushedBranch && m.flushedJump && m.flushedTrap);
    EXPECT_EQ(*m.flushedBranch + *m.flushedJump + *m.flushedTrap, *m.flushed) << "the flush split sums to the total";
    EXPECT_EQ(t.flushedBranch + t.flushedJump + t.flushedTrap, *m.flushed);

    // Every cycle is accounted for: four to fill the pipe, one per retired
    // instruction, and the stalled and flushed ones. (E8 pads these programs
    // so they run on nh_nf, so stalls are 0 and all four models agree.)
    EXPECT_EQ(m.cycles, m.retired + 4 + traceStalls + *m.flushed);
}

INSTANTIATE_TEST_SUITE_P(CacheLab, CacheLabTrace, ::testing::ValuesIn(allParams()));
