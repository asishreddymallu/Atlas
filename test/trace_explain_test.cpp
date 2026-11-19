/**
 * @file trace_explain_test.cpp
 * @brief The hazard explanation panel's sentences: every reason on every
 *        pipeline, word for word, and never a stall the trace did not record.
 *
 * Instructions are named by their source line (the label function below),
 * so the sentences read as the program does.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/trace_explain.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
struct Model
{
    const char   *name;
    ProcessorType type;
    ExplainModel  traits;
};
const Model kModels[] = {{"nh_nf", ProcessorType::RV5Stage_NH_NF, {false, false}},
                         {"nh_f", ProcessorType::RV5Stage_NH_F, {false, true}},
                         {"h_nf", ProcessorType::RV5Stage_H_NF, {true, false}},
                         {"h_f", ProcessorType::RV5Stage_H_F, {true, true}}};
enum
{
    NH_NF,
    NH_F,
    H_NF,
    H_F
};

/// The source's instruction lines, labels stripped: index = pc / 4.
std::vector<std::string> instructionLines(const std::string &source)
{
    std::vector<std::string> out;
    std::istringstream       in(source);
    std::string              line;
    bool                     text = false;
    while (std::getline(in, line))
    {
        if (line.find(".text") != std::string::npos)
        {
            text = true;
            continue;
        }
        if (line.find(".data") != std::string::npos)
            text = false;
        if (!text)
            continue;
        const auto colon = line.find(':');
        if (colon != std::string::npos)
            line = line.substr(colon + 1);
        const auto b = line.find_first_not_of(" \t");
        if (b != std::string::npos)
            out.push_back(line.substr(b));
    }
    return out;
}

struct Explained
{
    std::vector<TraceCycle>  records;
    std::vector<std::string> lines;
    ExplainModel             traits;

    Explained(const Model &m, const std::string &source) : lines(instructionLines(source)), traits(m.traits)
    {
        static QTemporaryDir dir;
        static int           serial = 0;
        const std::string    path = dir.path().toStdString() + "/ex" + std::to_string(serial++) + ".s";
        {
            std::ofstream file(path);
            file << source;
        }
        setupVmStateDirectory();
        ProcessorManager manager;
        manager.changeProcessor(m.type);
        manager.setStepDelay(0);
        manager.loadProgram(assemble(path));
        manager.setBreakpoints({});
        manager.run();
        records = manager.trace().all();
    }
    std::string label(uint64_t pc) const { return pc / 4 < lines.size() ? lines[pc / 4] : "?"; }
    /// The sentence for the instruction at `pc` in `cycle`.
    std::string at(uint32_t cycle, uint64_t pc) const
    {
        const TraceCycle &r = records.at(cycle - 1);
        for (const auto &o : r.occ)
            if (!o.bubble() && o.pc == pc)
                return explainTraceCell(records, cycle - 1, o.id, traits,
                                        [this](uint64_t p) { return label(p); });
        for (const auto &o : r.redirect.squashed)
            if (!o.bubble() && o.pc == pc)
                return explainTraceCell(records, cycle - 1, o.id, traits,
                                        [this](uint64_t p) { return label(p); });
        return "(not in the pipeline)";
    }
};

const char *kLoadUse = ".text\n    lw x5, 0(x0)\n    add x6, x5, x5\n    addi x7, x0, 1\n";
const char *kAluPair = ".text\n    addi x5, x0, 7\n    add x6, x5, x5\n    addi x7, x0, 1\n";
const char *kBranch  = ".text\n    beq x0, x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\n"
                       "    addi x7, x0, 3\nT:  addi x8, x0, 4\n";
const char *kJal     = ".text\n    jal x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\nT:  addi x7, x0, 3\n";
const char *kCsr     = ".text\n    csrrs x5, fflags, x0\n    addi x6, x0, 1\n    addi x7, x0, 2\n"
                       "    addi x8, x0, 3\n    addi x9, x0, 4\n";
} // namespace

TEST(TraceExplain, LoadUseStallWithForwarding)
{
    Explained e(kModels[H_F], kLoadUse);
    EXPECT_EQ(e.at(3, 4), "Cycle 3: add x6, x5, x5 waits in ID because lw x5, 0(x0), one instruction ahead, "
                          "has not loaded x5 yet. On this processor (hazard detection + forwarding) this "
                          "costs 1 stall cycle.");
    EXPECT_EQ(e.at(3, 8), "Cycle 3: addi x7, x0, 1 is fetched but stays in IF: the instruction ahead of it "
                          "is held in ID.");
    EXPECT_EQ(e.at(5, 4), "Cycle 5: add x6, x5, x5 executes in the ALU; x5 (rs1) comes from lw x5, 0(x0) "
                          "over the MEM/WB forwarding path; x5 (rs2) comes from lw x5, 0(x0) over the MEM/WB "
                          "forwarding path.");
}

TEST(TraceExplain, RawStallWithoutForwardingNamesBothCycles)
{
    for (const char *src : {kLoadUse, kAluPair})
    {
        Explained         e(kModels[H_NF], src);
        const std::string producer = instructionLines(src)[0];
        EXPECT_EQ(e.at(3, 4), "Cycle 3: add x6, x5, x5 waits in ID because " + producer +
                                  ", one instruction ahead, has not written x5 back yet and nothing forwards "
                                  "it. On this processor (hazard detection, no forwarding) this costs 2 stall "
                                  "cycles (stall cycle 1 of 2).");
        EXPECT_EQ(e.at(4, 4), "Cycle 4: add x6, x5, x5 waits in ID because " + producer +
                                  ", one instruction ahead, has not written x5 back yet and nothing forwards "
                                  "it. On this processor (hazard detection, no forwarding) this costs 2 stall "
                                  "cycles (stall cycle 2 of 2).");
        EXPECT_EQ(e.at(5, 4), "Cycle 5: add x6, x5, x5 is decoded and reads its registers.");
    }
}

TEST(TraceExplain, ForwardedDependenceDoesNotWait)
{
    for (size_t m : {size_t{NH_F}, size_t{H_F}})
    {
        Explained e(kModels[m], kAluPair);
        EXPECT_EQ(e.at(3, 4), "Cycle 3: add x6, x5, x5 reads x5, which addi x5, x0, 7, one instruction ahead, "
                              "has not written back yet. On this processor (" +
                                  modelPhrase(kModels[m].traits) +
                                  ") the forwarding unit hands it the value in EX, so it does not wait.")
            << kModels[m].name;
        EXPECT_EQ(e.at(4, 4), "Cycle 4: add x6, x5, x5 executes in the ALU; x5 (rs1) comes from addi x5, x0, 7 "
                              "over the EX/MEM forwarding path; x5 (rs2) comes from addi x5, x0, 7 over the "
                              "EX/MEM forwarding path.")
            << kModels[m].name;
    }
}

TEST(TraceExplain, NoHazardDetectionSaysToPad)
{
    {
        Explained e(kModels[NH_NF], kAluPair);
        EXPECT_EQ(e.at(3, 4), "Cycle 3: add x6, x5, x5 reads x5 in ID before addi x5, x0, 7, one instruction "
                              "ahead, has written it back. On this processor (no hazard detection, no "
                              "forwarding) nothing waits, so it reads the old value. Put 2 nops between them.");
    }
    {
        // Two apart: one nop is enough.
        Explained e(kModels[NH_NF], ".text\n    addi x5, x0, 7\n    nop\n    add x6, x5, x5\n");
        EXPECT_EQ(e.at(4, 8), "Cycle 4: add x6, x5, x5 reads x5 in ID before addi x5, x0, 7, two instructions "
                              "ahead, has written it back. On this processor (no hazard detection, no "
                              "forwarding) nothing waits, so it reads the old value. Put 1 nop between them.");
    }
    {
        Explained e(kModels[NH_F], kLoadUse);
        EXPECT_EQ(e.at(3, 4), "Cycle 3: add x6, x5, x5 needs x5 from lw x5, 0(x0), a load one instruction "
                              "ahead. On this processor (forwarding, no hazard detection) nothing waits for "
                              "the load, and its value is only ready at the end of MEM, so add x6, x5, x5 gets "
                              "a wrong value. Put 1 nop between them.");
        EXPECT_EQ(e.at(4, 4), "Cycle 4: add x6, x5, x5 executes in the ALU; x5 (rs1) comes from lw x5, 0(x0) "
                              "over the EX/MEM forwarding path, but a load in MEM has only its address there, "
                              "so the value is wrong (put 1 nop between them); x5 (rs2) comes from lw x5, "
                              "0(x0) over the EX/MEM forwarding path, but a load in MEM has only its address "
                              "there, so the value is wrong (put 1 nop between them).");
    }
}

TEST(TraceExplain, FlushesOnEveryPipeline)
{
    for (const Model &m : kModels)
    {
        Explained b(m, kBranch);
        EXPECT_EQ(b.at(4, 4), "Cycle 4: addi x5, x0, 1 is flushed from EX: beq x0, x0, T, a taken branch, "
                              "reached MEM and sent fetch to its target. The 3 slots behind it are discarded, "
                              "so 3 cycles are lost.")
            << m.name;
        EXPECT_EQ(b.at(4, 12), "Cycle 4: addi x7, x0, 3 is flushed from IF: beq x0, x0, T, a taken branch, "
                               "reached MEM and sent fetch to its target. The 3 slots behind it are "
                               "discarded, so 3 cycles are lost.")
            << m.name;
        Explained j(m, kJal);
        EXPECT_EQ(j.at(3, 4), "Cycle 3: addi x5, x0, 1 is flushed from ID: jal x0, T, a jump, reached EX and "
                              "sent fetch to its target. The 2 slots behind it are discarded, so 2 cycles are "
                              "lost.")
            << m.name;
        Explained c(m, kCsr);
        EXPECT_EQ(c.at(5, 4), "Cycle 5: addi x6, x0, 1 is flushed from MEM: csrrs x5, fflags, x0, a CSR "
                              "instruction, was served in WB and fetch restarts after it. The 4 slots behind "
                              "it are discarded, so 4 cycles are lost.")
            << m.name;
        EXPECT_EQ(b.at(1, 0), "Cycle 1: beq x0, x0, T is fetched from instruction memory.") << m.name;
        EXPECT_EQ(b.at(5, 0), "Cycle 5: beq x0, x0, T writes its result back to the register file.") << m.name;
    }
}

// The rule the panel lives by: a sentence speaks of stall cycles only for
// a cycle the trace recorded as stalled, and every stalled ID cell says so.
TEST(TraceExplain, NeverAStallTheTraceDidNotRecord)
{
    const char *programs[] = {kLoadUse, kAluPair, kBranch, kJal, kCsr,
                              ".text\n    lw x5, 0(x0)\n    lw x6, 0(x5)\n    add x7, x6, x5\n"
                              "    beq x7, x7, E\n    add x8, x7, x7\nE:  sw x7, 0(x0)\n"};
    for (const Model &m : kModels)
        for (const char *src : programs)
        {
            Explained e(m, src);
            for (size_t i = 0; i < e.records.size(); ++i)
                for (size_t s = 0; s < kTraceStages; ++s)
                {
                    const TraceOccupant &o = e.records[i].occ[s];
                    if (o.bubble())
                        continue;
                    const std::string text = explainTraceCell(e.records, i, o.id, m.traits,
                                                              [&](uint64_t p) { return e.label(p); });
                    const bool claims = text.find("stall cycle") != std::string::npos;
                    const bool stalledHere = e.records[i].stalled && s == 1;
                    EXPECT_EQ(claims, stalledHere) << m.name << " cycle " << i + 1 << ": " << text;
                    EXPECT_EQ(text.find("trace has no dependence"), std::string::npos) << text;
                }
        }
}
