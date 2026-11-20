/**
 * @file execution_trace_test.cpp
 * @brief WP-02: the ExecutionTrace against hand-drawn textbook timing
 *        diagrams and against timing computed in Python from the pipeline
 *        rules (never from the simulator), on all four pipelines.
 *
 * Diagram notation, one token per cycle from cycle 1:
 *   IF ID EX ME WB  the stage the instruction is in
 *   ID*             in ID, and the hazard unit stalls it this cycle
 *   IFx IDx EXx     squashed in this cycle (a redirect flushed it)
 *   .               not in the pipeline
 * Rows are instructions in order of first fetch.
 *
 * The rules the diagrams are drawn from (P&H 5-stage, as Atlas builds it):
 *  - every stage takes one cycle; WB writes before ID reads in a cycle;
 *  - h_f: a load followed by a reader of its rd stalls the reader 1 cycle
 *    in ID; everything else is forwarded (EX/MEM, then MEM/WB);
 *  - h_nf: a reader stalls in ID until its producer is in WB: 2 cycles
 *    behind a producer in EX, 1 behind one in MEM;
 *  - nh_nf, nh_f: never stall (the program must pad);
 *  - a jump resolves in EX (2 slots lost), a taken branch in MEM (3); the
 *    target is fetched the cycle after the redirect.
 */
#include "assembler/assembler.h"
#include "processor/execution_trace.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "processor/trace_json.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
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
};
// The order of timing_cases.inc.
const std::array<Model, 4> kModels = {{{"nh_nf", ProcessorType::RV5Stage_NH_NF},
                                       {"nh_f", ProcessorType::RV5Stage_NH_F},
                                       {"h_nf", ProcessorType::RV5Stage_H_NF},
                                       {"h_f", ProcessorType::RV5Stage_H_F}}};
enum
{
    NH_NF,
    NH_F,
    H_NF,
    H_F
};

AssembledProgram assembleSource(const std::string &source)
{
    static QTemporaryDir dir;
    static int           serial = 0;
    const std::string    path = dir.path().toStdString() + "/tr" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << source;
    }
    return assemble(path);
}

/// Loads and runs a program to its end on a fresh manager.
struct TraceRun
{
    ProcessorManager manager;
    TraceRun(ProcessorType type, const std::string &source)
    {
        setupVmStateDirectory();
        manager.changeProcessor(type);
        manager.setStepDelay(0);
        manager.loadProgram(assembleSource(source));
        manager.setBreakpoints({}); // known_issues 86
        manager.run();
    }
    std::vector<TraceCycle> records() const { return manager.trace().all(); }
};

const char *kToken[] = {"IF", "ID", "EX", "ME", "WB"};

/// The diagram the trace draws, in the notation above.
std::vector<std::string> diagram(const std::vector<TraceCycle> &records)
{
    std::vector<uint32_t>                         order;
    std::map<uint32_t, std::vector<std::string>> rows;
    const size_t                                  cycles = records.size();
    for (size_t c = 0; c < cycles; ++c)
    {
        const TraceCycle &r = records[c];
        for (size_t s = 0; s < kTraceStages; ++s)
        {
            const TraceOccupant &o = r.occ[s];
            if (o.bubble())
                continue;
            if (!rows.count(o.id))
            {
                order.push_back(o.id);
                rows[o.id].assign(cycles, ".");
            }
            std::string token = kToken[s];
            if (s == 1 && r.stalled)
                token += "*";
            if (std::find(r.redirect.squashed.begin(), r.redirect.squashed.end(), o) !=
                r.redirect.squashed.end())
                token += "x";
            rows[o.id][c] = token;
        }
    }
    std::vector<std::string> out;
    for (uint32_t id : order)
    {
        auto &cells = rows[id];
        while (!cells.empty() && cells.back() == ".")
            cells.pop_back();
        std::string line;
        for (const auto &cell : cells)
            line += (line.empty() ? "" : " ") + cell;
        out.push_back(line);
    }
    return out;
}

std::string joined(const std::vector<std::string> &rows)
{
    std::string s;
    for (const auto &r : rows)
        s += r + "\n";
    return s;
}

// ---- The four canonical programs ----------------------------------------
const char *kLoadUse = ".text\n    lw x5, 0(x0)\n    add x6, x5, x5\n    addi x7, x0, 1\n";
const char *kAluPair = ".text\n    addi x5, x0, 7\n    add x6, x5, x5\n    addi x7, x0, 1\n";
const char *kBranch  = ".text\n    beq x0, x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\n"
                       "    addi x7, x0, 3\nT:  addi x8, x0, 4\n";
const char *kJal     = ".text\n    jal x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\nT:  addi x7, x0, 3\n";

const std::vector<std::string> kNoStall = {"IF ID EX ME WB", ". IF ID EX ME WB",
                                           ". . IF ID EX ME WB"};
// h_nf: the reader waits in ID until the producer is in WB (cycle 5).
const std::vector<std::string> kTwoStalls = {"IF ID EX ME WB", ". IF ID* ID* ID EX ME WB",
                                             ". . IF IF IF ID EX ME WB"};
// h_f: load-use, one bubble; the loaded x5 reaches EX by MEM/WB in cycle 5.
const std::vector<std::string> kOneStall = {"IF ID EX ME WB", ". IF ID* ID EX ME WB",
                                            ". . IF IF ID EX ME WB"};
// A taken beq resolves in MEM (cycle 4): IF, ID and EX are squashed there.
const std::vector<std::string> kBranchRows = {"IF ID EX ME WB", ". IF ID EXx", ". . IF IDx",
                                              ". . . IFx", ". . . . IF ID EX ME WB"};
// jal resolves in EX (cycle 3): IF and ID are squashed there.
const std::vector<std::string> kJalRows = {"IF ID EX ME WB", ". IF IDx", ". . IFx",
                                           ". . . IF ID EX ME WB"};

class TraceHand : public ::testing::TestWithParam<size_t>
{
};
std::string modelName(const ::testing::TestParamInfo<size_t> &info) { return kModels[info.param].name; }
} // namespace

TEST_P(TraceHand, LoadUse)
{
    const size_t m = GetParam();
    TraceRun          run(kModels[m].type, kLoadUse);
    const auto   records = run.records();
    const auto  &want    = m == H_NF ? kTwoStalls : m == H_F ? kOneStall : kNoStall;
    EXPECT_EQ(joined(diagram(records)), joined(want));
    ASSERT_EQ(records.size(), run.manager.getCycles());

    const TraceCycle &c3 = records[2];
    ASSERT_EQ(c3.raw.size(), 1u); // add x6, x5, x5 in ID reads x5 of lw in EX (once: rs1 == rs2)
    EXPECT_EQ(c3.raw[0].reg, 5);
    EXPECT_EQ(c3.raw[0].producerStage, TraceStage::EX);
    EXPECT_TRUE(c3.raw[0].producerIsLoad);
    const RawResolution res[] = {RawResolution::Unhandled, RawResolution::Unhandled,
                                 RawResolution::Stall, RawResolution::Stall};
    EXPECT_EQ(c3.raw[0].resolution, res[m]);
    if (m == H_F)
    {
        EXPECT_TRUE(c3.stalled);
        EXPECT_EQ(c3.stall.reason, StallReason::LoadUse);
        EXPECT_EQ(c3.stall.reg, 5);
        EXPECT_EQ(c3.stall.producerPc, 0u);
        EXPECT_EQ(c3.stall.length, 1);
        // Cycle 5: add in EX takes both operands from lw in WB (MEM/WB).
        ASSERT_EQ(records[4].fwd.size(), 2u);
        for (const auto &f : records[4].fwd)
        {
            EXPECT_EQ(f.from, TraceStage::WB);
            EXPECT_EQ(f.reg, 5);
            EXPECT_TRUE(f.producerIsLoad);
        }
    }
    if (m == H_NF)
    {
        EXPECT_EQ(c3.stall.reason, StallReason::RawNoForwarding);
        EXPECT_EQ(c3.stall.length, 2);
        EXPECT_FALSE(c3.stall.continuing);
        EXPECT_TRUE(records[3].stalled);
        EXPECT_TRUE(records[3].stall.continuing);
        EXPECT_EQ(records[3].stall.reason, StallReason::RawNoForwarding);
        for (const auto &r : records)
            EXPECT_TRUE(r.fwd.empty()) << "no forwarding on h_nf, cycle " << r.cycle;
    }
    if (m == NH_NF || m == NH_F)
        for (const auto &r : records)
            EXPECT_FALSE(r.stalled) << "cycle " << r.cycle;
}

TEST_P(TraceHand, DependentAluPair)
{
    const size_t m = GetParam();
    TraceRun          run(kModels[m].type, kAluPair);
    const auto   records = run.records();
    EXPECT_EQ(joined(diagram(records)), joined(m == H_NF ? kTwoStalls : kNoStall));
    ASSERT_EQ(records.size(), run.manager.getCycles());
    const RawResolution res[] = {RawResolution::Unhandled, RawResolution::Forward,
                                 RawResolution::Stall, RawResolution::Forward};
    ASSERT_EQ(records[2].raw.size(), 1u);
    EXPECT_FALSE(records[2].raw[0].producerIsLoad);
    EXPECT_EQ(records[2].raw[0].resolution, res[m]);
    if (m == NH_F || m == H_F)
    {
        // Cycle 4: add in EX, addi in MEM: both operands over EX/MEM.
        ASSERT_EQ(records[3].fwd.size(), 2u);
        EXPECT_EQ(records[3].fwd[0].operand, 1);
        EXPECT_EQ(records[3].fwd[1].operand, 2);
        for (const auto &f : records[3].fwd)
        {
            EXPECT_EQ(f.from, TraceStage::MEM);
            EXPECT_EQ(f.producerPc, 0u);
        }
    }
    else
        for (const auto &r : records)
            EXPECT_TRUE(r.fwd.empty());
}

TEST_P(TraceHand, TakenBranch)
{
    TraceRun        run(kModels[GetParam()].type, kBranch);
    const auto records = run.records();
    EXPECT_EQ(joined(diagram(records)), joined(kBranchRows));
    ASSERT_EQ(records.size(), 9u);
    EXPECT_EQ(run.manager.getCycles(), 9u);
    const TraceCycle &c4 = records[3];
    EXPECT_EQ(c4.redirect.kind, RedirectKind::Branch);
    EXPECT_EQ(c4.redirect.byPc, 0u);
    ASSERT_EQ(c4.redirect.squashed.size(), 3u);
    EXPECT_EQ(c4.redirect.squashed[0].pc, 12u); // IF
    EXPECT_EQ(c4.redirect.squashed[1].pc, 8u);  // ID
    EXPECT_EQ(c4.redirect.squashed[2].pc, 4u);  // EX
    for (const auto &r : records)
        if (r.cycle != 4)
            EXPECT_EQ(r.redirect.kind, RedirectKind::None) << "cycle " << r.cycle;
}

TEST_P(TraceHand, Jal)
{
    TraceRun        run(kModels[GetParam()].type, kJal);
    const auto records = run.records();
    EXPECT_EQ(joined(diagram(records)), joined(kJalRows));
    ASSERT_EQ(records.size(), 8u);
    const TraceCycle &c3 = records[2];
    EXPECT_EQ(c3.redirect.kind, RedirectKind::Jump);
    ASSERT_EQ(c3.redirect.squashed.size(), 2u);
    EXPECT_EQ(c3.redirect.squashed[0].pc, 8u);
    EXPECT_EQ(c3.redirect.squashed[1].pc, 4u);
}

INSTANTIATE_TEST_SUITE_P(Pipelines, TraceHand, ::testing::Values(0, 1, 2, 3), modelName);

// ---- Generated programs: timing from the Python rules --------------------
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
constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

struct GenParam
{
    size_t c;
    size_t m;
};
void PrintTo(const GenParam &p, std::ostream *os) { *os << kCases[p.c].name << "_" << kModels[p.m].name; }
std::vector<GenParam> allGen()
{
    std::vector<GenParam> v;
    for (size_t c = 0; c < kCaseCount; ++c)
        for (size_t m = 0; m < 4; ++m)
            v.push_back({c, m});
    return v;
}
class TraceGenerated : public ::testing::TestWithParam<GenParam>
{
};

/// The structural invariants every pipeline trace must keep.
void checkInvariants(const std::vector<TraceCycle> &records, const ProcessorManager &manager)
{
    ASSERT_EQ(records.size(), manager.getCycles()) << "one record per cycle";
    unsigned stalls = 0;
    std::map<uint32_t, int> lastStage; // id -> furthest stage index seen
    std::set<uint32_t>      squashed, retired;
    for (size_t i = 0; i < records.size(); ++i)
    {
        const TraceCycle &r = records[i];
        EXPECT_EQ(r.cycle, i + 1);
        stalls += r.stalled ? 1 : 0;
        if (r.stalled)
            EXPECT_NE(r.stall.reason, StallReason::Unknown) << "cycle " << r.cycle;
        // Every squash belongs to a redirect, within its reach.
        if (r.redirect.kind == RedirectKind::None)
            EXPECT_TRUE(r.redirect.squashed.empty()) << "cycle " << r.cycle;
        const size_t reach = r.redirect.kind == RedirectKind::Jump     ? 2
                             : r.redirect.kind == RedirectKind::Branch ? 3
                             : r.redirect.kind == RedirectKind::None   ? 0
                                                                       : 4;
        EXPECT_EQ(r.redirect.squashed.size(), reach) << "cycle " << r.cycle;
        // Each instance moves forward one stage a cycle or holds (IF/ID).
        for (size_t s = 0; s < kTraceStages; ++s)
        {
            const TraceOccupant &o = r.occ[s];
            if (o.bubble())
                continue;
            if (lastStage.count(o.id))
            {
                const int prev = lastStage[o.id];
                EXPECT_TRUE(prev == static_cast<int>(s) - 1 || (prev == static_cast<int>(s) && s <= 1))
                    << "id " << o.id << " from stage " << prev << " to " << s << " at cycle " << r.cycle;
            }
            EXPECT_FALSE(squashed.count(o.id)) << "id " << o.id << " squashed earlier, back at cycle " << r.cycle;
            lastStage[o.id] = static_cast<int>(s);
            if (s == 4)
            {
                EXPECT_FALSE(retired.count(o.id)) << "retired twice";
                retired.insert(o.id);
            }
        }
        for (const auto &o : r.redirect.squashed)
            if (!o.bubble())
                squashed.insert(o.id);
    }
    EXPECT_EQ(stalls, manager.metrics().stalls.value_or(0)) << "trace stalls == Profiler stalls";
    EXPECT_EQ(retired.size(), manager.getInstructionsRetired());
    for (const auto &[id, stage] : lastStage)
        EXPECT_TRUE(stage == 4 || squashed.count(id)) << "id " << id << " neither retired nor squashed";
}
} // namespace

TEST_P(TraceGenerated, MatchesTheRules)
{
    const TimingCase  &tc   = kCases[GetParam().c];
    const ModelTiming &want = tc.model[GetParam().m];
    TraceRun                run(kModels[GetParam().m].type, tc.source);
    const auto         records = run.records();
    checkInvariants(records, run.manager);

    EXPECT_EQ(records.size(), want.cycles) << tc.source;
    // The Python stream holds only instructions that retire. The hazard unit
    // also stalls a wrong-path instruction that a taken branch then squashes
    // (gen24 h_nf: `addi x5, x7, 1` after `beq x8, x8, L4` waits in ID one
    // cycle, then the branch flushes it). The machine counts that cycle and
    // so does the trace (checkInvariants: trace == Profiler); the comparison
    // with the rules is over the stalls of retired instructions.
    std::set<uint32_t> retiredIds;
    for (const auto &r : records)
        if (!r.occ[4].bubble())
            retiredIds.insert(r.occ[4].id);
    unsigned stalls = 0;
    for (const auto &r : records)
        stalls += r.stalled && retiredIds.count(r.occ[1].id);
    EXPECT_EQ(stalls, want.stalls) << joined(diagram(records));

    // Per instance that retired: the cycle it entered each stage.
    std::map<uint32_t, std::array<unsigned, 5>> first;
    std::map<uint32_t, unsigned>                stallsOf;
    std::map<uint32_t, uint64_t>                pcOf;
    std::vector<uint32_t>                       order;
    for (const auto &r : records)
        for (size_t s = 0; s < kTraceStages; ++s)
        {
            const auto &o = r.occ[s];
            if (o.bubble())
                continue;
            if (!first.count(o.id))
            {
                first[o.id] = {};
                order.push_back(o.id);
                pcOf[o.id] = o.pc;
            }
            if (!first[o.id][s])
                first[o.id][s] = r.cycle;
            if (s == 1 && r.stalled)
                ++stallsOf[o.id];
        }
    std::vector<Row> got;
    for (uint32_t id : order)
        if (first[id][4])
            got.push_back({pcOf[id], first[id][0], first[id][1], first[id][2], first[id][3], first[id][4],
                           stallsOf[id]});
    ASSERT_EQ(got.size(), want.rows.size()) << tc.source;
    for (size_t i = 0; i < got.size(); ++i)
    {
        const Row &g = got[i], &w = want.rows[i];
        EXPECT_TRUE(g.pc == w.pc && g.fetch == w.fetch && g.id == w.id && g.ex == w.ex && g.mem == w.mem &&
                    g.wb == w.wb && g.stalls == w.stalls)
            << "instruction " << i << " pc " << g.pc << ": got IF " << g.fetch << " ID " << g.id << " EX "
            << g.ex << " MEM " << g.mem << " WB " << g.wb << " stalls " << g.stalls << "; want pc " << w.pc
            << " IF " << w.fetch << " ID " << w.id << " EX " << w.ex << " MEM " << w.mem << " WB " << w.wb
            << " stalls " << w.stalls;
    }
}

INSTANTIATE_TEST_SUITE_P(Rules, TraceGenerated, ::testing::ValuesIn(allGen()));

// ---- Undo, Redo, JSON, the ring, RVSS ------------------------------------
TEST(ExecutionTrace, UndoTruncatesAndRedoReRecordsTheSameCycles)
{
    for (const Model &m : kModels)
    {
        TraceRun        run(m.type, kLoadUse);
        const auto full = run.records();
        ASSERT_GE(full.size(), 4u);
        for (int i = 0; i < 3; ++i)
            run.manager.undo();
        EXPECT_EQ(run.manager.trace().size(), full.size() - 3) << m.name;
        EXPECT_EQ(run.manager.trace().lastCycle(), run.manager.getCycles()) << m.name;
        for (int i = 0; i < 3; ++i)
            run.manager.redo();
        EXPECT_EQ(run.records(), full) << m.name << ": Redo must restore the same records";
        // Undo then a fresh Step re-records the same cycle too.
        run.manager.undo();
        run.manager.undo();
        run.manager.step();
        run.manager.step();
        EXPECT_EQ(run.records(), full) << m.name << ": re-stepping must record the same cycles";
    }
}

TEST(ExecutionTrace, JsonRoundTrip)
{
    for (const Model &m : kModels)
    {
        TraceRun run(m.type, kCases[3].source);
        const QByteArray bytes = QJsonDocument(run.manager.traceJson()).toJson();
        const QJsonObject parsed = QJsonDocument::fromJson(bytes).object();
        std::vector<TraceCycle> back;
        TraceMeta               meta;
        ASSERT_TRUE(traceFromJson(parsed, back, meta)) << m.name;
        EXPECT_EQ(back, run.records()) << m.name;
        EXPECT_EQ(meta.model, QString::fromLatin1(m.name));
        EXPECT_EQ(parsed["schema"].toString(), QStringLiteral("atlas.trace/1"));
        // The occupants carry their source text.
        const QJsonObject wb = parsed["cycles"].toArray()[4].toObject()["stages"].toObject()["WB"].toObject();
        EXPECT_FALSE(wb["disassembly"].toString().isEmpty()) << m.name;
        EXPECT_TRUE(wb["disassembly"].toString().startsWith("lui")) << wb["disassembly"].toString().toStdString();
        EXPECT_EQ(wb["line"].toInt(), 4) << m.name; // .data, D:, .text, lui
    }
}

TEST(ExecutionTrace, ExportWritesTheFile)
{
    TraceRun           run(ProcessorType::RV5Stage_H_F, kLoadUse);
    QTemporaryDir dir;
    const QString path = dir.filePath("trace.json");
    QString       error;
    ASSERT_TRUE(run.manager.exportTraceJson(path, &error)) << error.toStdString();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject j = QJsonDocument::fromJson(file.readAll()).object();
    EXPECT_EQ(j["cycles"].toArray().size(), 8);
    EXPECT_TRUE(j["hazard_detection"].toBool());
    EXPECT_TRUE(j["forwarding"].toBool());
}

TEST(ExecutionTrace, TheRingKeepsTheNewestCapacityCycles)
{
    ExecutionTrace ring(5);
    EXPECT_EQ(ExecutionTrace().capacity(), 100000u);
    for (uint32_t c = 1; c <= 12; ++c)
    {
        TraceCycle r;
        r.cycle = c;
        ring.push(r);
    }
    EXPECT_EQ(ring.size(), 5u);
    EXPECT_EQ(ring.dropped(), 7u);
    EXPECT_EQ(ring.firstCycle(), 8u);
    EXPECT_EQ(ring.lastCycle(), 12u);
    const auto w = ring.window(10, 2);
    ASSERT_EQ(w.size(), 2u);
    EXPECT_EQ(w[0].cycle, 10u);
    EXPECT_EQ(w[1].cycle, 11u);
    ring.truncateAfter(9);
    EXPECT_EQ(ring.lastCycle(), 9u);
    EXPECT_EQ(ring.size(), 2u);
}

// RVSS is single-cycle: one instruction occupies the one stage per cycle.
TEST(ExecutionTrace, RvssRecordsOneOccupiedStagePerCycle)
{
    TraceRun        run(ProcessorType::RVSS, kLoadUse);
    const auto records = run.records();
    ASSERT_EQ(records.size(), 3u);
    for (size_t i = 0; i < records.size(); ++i)
    {
        EXPECT_TRUE(records[i].singleCycle);
        EXPECT_EQ(records[i].occ[0].pc, 4 * i);
        for (size_t s = 1; s < kTraceStages; ++s)
            EXPECT_TRUE(records[i].occ[s].bubble());
        EXPECT_FALSE(records[i].stalled);
    }
    run.manager.undo();
    EXPECT_EQ(run.manager.trace().size(), 2u);
    run.manager.redo();
    EXPECT_EQ(run.records(), records);
    const QJsonObject j = run.manager.traceJson();
    EXPECT_TRUE(j["single_cycle"].toBool());
    EXPECT_TRUE(j["cycles"].toArray()[0].toObject()["stages"].toObject().contains("CI"));
}

// ---- T4: every producer and "by" reference carries its instruction word -----
// forwarding[].producer.instruction and redirect.by.instruction were written
// as 0x00000000 (TraceForward and TraceRedirect carry no word); raw[] and
// stall producers were right. The word of an id is the word of the stage
// occupant with that id, anywhere in the trace.
namespace
{
const std::array<Model, 5> kAllModels = {{{"rvss", ProcessorType::RVSS},
                                          {"nh_nf", ProcessorType::RV5Stage_NH_NF},
                                          {"nh_f", ProcessorType::RV5Stage_NH_F},
                                          {"h_nf", ProcessorType::RV5Stage_H_NF},
                                          {"h_f", ProcessorType::RV5Stage_H_F}}};

class TraceJsonWords : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(TraceJsonWords, EveryProducerCarriesItsInstructionWord)
{
    const Model &m       = kAllModels[GetParam()];
    size_t       checked = 0, forwarding = 0, redirects = 0, unknownIds = 0;
    for (size_t c = 0; c < kCaseCount; ++c)
    {
        TraceRun          run(m.type, kCases[c].source);
        const QJsonObject j = run.manager.traceJson();
        // id -> word, from every stage occupant of every cycle.
        std::map<qint64, QString> word;
        for (const QJsonValue &cv : j["cycles"].toArray())
        {
            const QJsonObject stages = cv.toObject()["stages"].toObject();
            for (const QString &k : stages.keys())
                if (stages[k].isObject())
                    word[stages[k].toObject()["id"].toInteger()] = stages[k].toObject()["instruction"].toString();
        }
        auto check = [&](const QJsonObject &ref, const char *what, qint64 cycle)
        {
            const qint64 id = ref["id"].toInteger();
            if (id == 0)
                return;
            const auto it = word.find(id);
            if (it == word.end())
            {
                ++unknownIds; // no occupant has it: the word must stay 0
                EXPECT_EQ(ref["instruction"].toString().toStdString(), "0x00000000") << what;
                return;
            }
            ++checked;
            EXPECT_EQ(ref["instruction"].toString().toStdString(), it->second.toStdString())
                << kCases[c].name << " " << m.name << " cycle " << cycle << " " << what << " id " << id;
        };
        for (const QJsonValue &cv : j["cycles"].toArray())
        {
            const QJsonObject cy    = cv.toObject();
            const qint64      cycle = cy["cycle"].toInteger();
            for (const QJsonValue &x : cy["raw"].toArray())
                check(x.toObject()["producer"].toObject(), "raw.producer", cycle);
            if (cy["stall"].isObject())
                check(cy["stall"].toObject()["producer"].toObject(), "stall.producer", cycle);
            for (const QJsonValue &f : cy["forwarding"].toArray())
            {
                ++forwarding;
                check(f.toObject()["producer"].toObject(), "forwarding.producer", cycle);
            }
            if (cy["redirect"].isObject())
            {
                ++redirects;
                check(cy["redirect"].toObject()["by"].toObject(), "redirect.by", cycle);
            }
        }
    }
    RecordProperty("checked", static_cast<int>(checked));
    std::cout << "[words] " << m.name << " references checked " << checked << " (forwarding " << forwarding
              << ", redirects " << redirects << "), ids with no occupant " << unknownIds << "\n";
    if (m.type == ProcessorType::RV5Stage_NH_F || m.type == ProcessorType::RV5Stage_H_F)
        EXPECT_GT(forwarding, 0u) << "the forwarding models must exercise forwarding[]";
    if (m.type != ProcessorType::RVSS)
        EXPECT_GT(redirects, 0u);
}

INSTANTIATE_TEST_SUITE_P(Models, TraceJsonWords, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kAllModels[info.param].name);
                         });
