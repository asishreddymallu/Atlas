/**
 * @file profiler_provenance_test.cpp
 * @brief WP-05: the Profiler's stall and flush splits, and the
 *        [Measured] / [Modelled] tag on every row.
 *
 * The splits come from the ExecutionTrace. Each must sum to its total:
 * load-use + RAW without forwarding == Stall cycles (the machine's own
 * counter), and branch + jump + CSR/ecall == Flushed cycles == the slots
 * the trace recorded as squashed. Checked on the 64 generated programs of
 * timing_cases.inc and on the hand programs, on every model, and again
 * after Undo.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "ui/common/processor_metrics_model.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <fstream>
#include <string>

using namespace Atlas;

namespace
{
struct Row
{
    uint64_t pc;
    unsigned fetch, id, ex, mem, wb, stalls;
};
struct ModelTiming
{
    unsigned         cycles;
    unsigned         stalls;
    std::vector<Row> rows;
};
struct TimingCase
{
    const char *name;
    const char *source;
    ModelTiming model[4];
};
const TimingCase kCases[] = {
#include "timing_cases.inc"
};

const ProcessorType kTypes[] = {ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_NH_F,
                                ProcessorType::RV5Stage_H_NF, ProcessorType::RV5Stage_H_F};

void load(ProcessorManager &manager, ProcessorType type, const std::string &source)
{
    static QTemporaryDir dir;
    static int           serial = 0;
    const std::string    path = dir.path().toStdString() + "/pv" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << source;
    }
    setupVmStateDirectory();
    manager.changeProcessor(type);
    manager.setStepDelay(0);
    manager.loadProgram(assemble(path));
    manager.setBreakpoints({});
}

void checkSplits(const ProcessorManager &manager, ProcessorType type, const std::string &what)
{
    const ProcessorMetrics m = manager.metrics();
    const bool hdu = type == ProcessorType::RV5Stage_H_NF || type == ProcessorType::RV5Stage_H_F;
    if (type == ProcessorType::RVSS)
    {
        EXPECT_FALSE(m.flushed) << what;
        EXPECT_FALSE(m.stallsLoadUse) << what;
        return;
    }
    ASSERT_TRUE(m.flushed) << what;
    EXPECT_EQ(*m.flushed, *m.flushedBranch + *m.flushedJump + *m.flushedTrap) << what;
    const TraceTotals t = traceTotals(manager.trace().all());
    EXPECT_EQ(*m.flushedBranch, t.flushedBranch) << what;
    EXPECT_EQ(*m.flushedJump, t.flushedJump) << what;
    EXPECT_EQ(*m.flushedTrap, t.flushedTrap) << what;
    EXPECT_EQ(*m.flushedBranch, 3 * *m.mispredictions) << what << ": 3 slots per taken branch";
    if (hdu)
    {
        ASSERT_TRUE(m.stalls && m.stallsLoadUse && m.stallsRawNoForwarding) << what;
        EXPECT_EQ(*m.stallsLoadUse + *m.stallsRawNoForwarding, *m.stalls) << what << ": the split sums to the total";
        if (type == ProcessorType::RV5Stage_H_F)
            EXPECT_EQ(*m.stallsRawNoForwarding, 0u) << what << ": h_f only stalls for load-use";
        else
            EXPECT_EQ(*m.stallsLoadUse, 0u) << what << ": h_nf names every stall RAW without forwarding";
    }
    else
    {
        EXPECT_FALSE(m.stalls) << what;
        EXPECT_FALSE(m.stallsLoadUse) << what;
        EXPECT_FALSE(m.stallsRawNoForwarding) << what;
    }
}
} // namespace

TEST(ProfilerProvenance, SplitsSumToTheirTotalsOnEveryGeneratedProgram)
{
    for (const TimingCase &c : kCases)
        for (ProcessorType type : kTypes)
        {
            ProcessorManager manager;
            load(manager, type, c.source);
            manager.run();
            const std::string what = std::string(c.name) + " model " + std::to_string(int(type));
            checkSplits(manager, type, what);
            // Undo takes cycles back off the splits as well.
            for (int i = 0; i < 7; ++i)
                manager.undo();
            checkSplits(manager, type, what + " after 7 Undo");
            for (int i = 0; i < 7; ++i)
                manager.redo();
            checkSplits(manager, type, what + " after 7 Redo");
        }
}

TEST(ProfilerProvenance, HandProgramsHaveTheExpectedSplit)
{
    // lw then a reader: h_f 1 load-use, h_nf 2 RAW. A taken beq: 3 slots; jal: 2.
    const char *src = ".text\n    lw x5, 0(x0)\n    add x6, x5, x5\n    beq x0, x0, T\n    addi x7, x0, 1\n"
                      "T:  jal x0, U\n    addi x8, x0, 1\nU:  csrrs x9, fflags, x0\n    addi x9, x0, 2\n";
    struct Want
    {
        ProcessorType type;
        unsigned      loadUse, raw;
    } wants[] = {{ProcessorType::RV5Stage_H_F, 1, 0}, {ProcessorType::RV5Stage_H_NF, 0, 2}};
    for (const Want &w : wants)
    {
        ProcessorManager manager;
        load(manager, w.type, src);
        manager.run();
        const ProcessorMetrics m = manager.metrics();
        EXPECT_EQ(*m.stallsLoadUse, w.loadUse);
        EXPECT_EQ(*m.stallsRawNoForwarding, w.raw);
        EXPECT_EQ(*m.flushedBranch, 3u);
        EXPECT_EQ(*m.flushedJump, 2u);
        EXPECT_EQ(*m.flushedTrap, 4u);
        EXPECT_EQ(*m.flushed, 9u);
    }
}

TEST(ProfilerProvenance, EveryRowIsTagged)
{
    const std::pair<ProfilerRow, const char *> tags[] = {
        {ProfilerRow::Cycles, "[Measured]"},         {ProfilerRow::Retired, "[Measured]"},
        {ProfilerRow::CPI, "[Modelled]"},            {ProfilerRow::IPC, "[Modelled]"},
        {ProfilerRow::Stalls, "[Measured]"},         {ProfilerRow::StallsLoadUse, "[Measured]"},
        {ProfilerRow::StallsRawNoForwarding, "[Measured]"}, {ProfilerRow::Flushed, "[Measured]"},
        {ProfilerRow::FlushedBranch, "[Measured]"},  {ProfilerRow::FlushedJump, "[Measured]"},
        {ProfilerRow::FlushedTrap, "[Measured]"},    {ProfilerRow::Mispredictions, "[Measured]"}};
    ASSERT_EQ(std::size(tags), static_cast<size_t>(ProfilerRow::Count));

    ProcessorManager manager;
    load(manager, ProcessorType::RV5Stage_H_F, ".text\n    lw x5, 0(x0)\n    add x6, x5, x5\n");
    manager.run();
    ProcessorMetricsModel model(nullptr, &manager);
    ASSERT_EQ(model.columnCount(), 3);
    ASSERT_EQ(model.rowCount(), static_cast<int>(ProfilerRow::Count));
    EXPECT_EQ(model.headerData(2, Qt::Horizontal).toString(), "Source");
    for (const auto &[row, tag] : tags)
    {
        const int r = static_cast<int>(row);
        EXPECT_EQ(model.data(model.index(r, 2)).toString(), QString::fromLatin1(tag)) << r;
        EXPECT_TRUE(model.data(model.index(r, 1), Qt::ToolTipRole).toString().startsWith(tag)) << r;
        EXPECT_FALSE(model.data(model.index(r, 2), Qt::ToolTipRole).toString().isEmpty()) << r;
    }
    EXPECT_EQ(model.data(model.index(int(ProfilerRow::StallsLoadUse), 1)).toUInt(), 1u);
    EXPECT_EQ(model.data(model.index(int(ProfilerRow::StallsRawNoForwarding), 1)).toUInt(), 0u);
    EXPECT_EQ(model.data(model.index(int(ProfilerRow::Stalls), 1)).toUInt(), 1u);

    // RVSS: the split rows read n/a, still tagged.
    load(manager, ProcessorType::RVSS, ".text\n    addi x5, x0, 1\n");
    manager.run();
    model.refresh();
    for (ProfilerRow row : {ProfilerRow::StallsLoadUse, ProfilerRow::Flushed, ProfilerRow::FlushedBranch})
        EXPECT_EQ(model.data(model.index(int(row), 1)).toString(), "n/a");
}
