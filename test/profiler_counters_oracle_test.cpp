/**
 * @file profiler_counters_oracle_test.cpp
 * @brief Every run counter the window shows (cycles, retired, CPI, IPC,
 *        stall cycles, branch mispredictions) against values worked out by
 *        hand, on all five models, after Run and after every Step, Undo
 *        and Redo; and their reset on reload and on processor switch.
 *
 * The rules the expectations come from (not from the code under test):
 *
 *  rvss: one instruction per cycle. cycles = retired; CPI = IPC = 1.
 *        Nothing stalls and nothing is predicted: both read n/a.
 *
 *  5-stage (IF ID EX MEM WB): the first instruction retires in cycle 5,
 *  each later one a cycle after the one before, unless the pipeline loses
 *  cycles. So cycles = retired + 4 + lost cycles, where:
 *   - a data stall (h_nf, h_f only) is one lost cycle each;
 *     h_f forwards, so only a load followed directly by its use stalls,
 *     once; h_nf does not forward, so a consumer directly behind its
 *     producer waits for write-back: 2 stalls (ALU or load alike);
 *     nh_nf and nh_f never stall (no hazard unit): n/a;
 *   - a redirect decided in stage S (IF=1 .. WB=5) loses S-1 cycles: every
 *     younger instruction is discarded, and the target is fetched in the
 *     next cycle. A taken branch is decided in MEM: 3. jal in EX: 2. An
 *     ecall and a Zicsr instruction are served at WB and refetch from
 *     pc+4: 4. (The E1 comment at rv5s_processor_base.cpp:814 and the U5
 *     brief say three for ecall; the rule gives four, and four is what
 *     runs: known_issues 143.)
 *   - fetch assumes not taken: each taken conditional branch is one
 *     misprediction; a not-taken one and a jal are none.
 *  CPI = cycles / retired, IPC = retired / cycles.
 *
 * After an Undo every counter equals what it was before the step; after
 * the Redo, what it was after. A reload or a switch reads all zero.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using namespace Atlas;

namespace
{
enum M
{
    RVSS,
    NH_NF,
    H_NF,
    NH_F,
    H_F,
    MODELS
};

const char *kModelName[MODELS] = {"rvss", "nh_nf", "h_nf", "nh_f", "h_f"};
const ProcessorType kType[MODELS] = {ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF,
                                     ProcessorType::RV5Stage_H_NF, ProcessorType::RV5Stage_NH_F,
                                     ProcessorType::RV5Stage_H_F};

struct Expected
{
    unsigned cycles;
    unsigned stalls;         // ignored where the model has none (n/a)
    unsigned mispredictions; // ignored on rvss (n/a)
};

struct Case
{
    const char *name;
    const char *source;
    unsigned    retired; // the same on every model
    Expected    per[MODELS];
};

const std::vector<Case> &cases()
{
    static const std::vector<Case> c = {
        // N = 3 independent instructions. rvss 3. Pipelines 3 + 4 = 7.
        {"StraightLine",
         ".text\n addi t0, x0, 1\n addi t1, x0, 2\n addi t2, x0, 3\n",
         3,
         {{3, 0, 0}, {7, 0, 0}, {7, 0, 0}, {7, 0, 0}, {7, 0, 0}}},
        // Load then its use. h_f: 1 stall -> 2+4+1 = 7. h_nf: 2 -> 8.
        // nh_*: no stall, 6 (the value is wrong there; the timing is not).
        {"LoadUse",
         ".text\n ld t0, 0(x0)\n addi t1, t0, 1\n",
         2,
         {{2, 0, 0}, {6, 0, 0}, {8, 2, 0}, {6, 0, 0}, {7, 1, 0}}},
        // ALU producer then its use. h_f forwards: 0 stalls, 6. h_nf: 2, 8.
        {"AluUse",
         ".text\n addi t0, x0, 1\n addi t1, t0, 1\n",
         2,
         {{2, 0, 0}, {6, 0, 0}, {8, 2, 0}, {6, 0, 0}, {6, 0, 0}}},
        // Taken branch: beq and the target retire (2); the two between are
        // skipped. Pipelines: 2 + 4 + 3 (decided in MEM) = 9; 1 mispredict.
        {"TakenBranch",
         ".text\n beq x0, x0, T\n addi t1, x0, 1\n addi t2, x0, 2\nT:\n addi t3, x0, 3\n",
         2,
         {{2, 0, 0}, {9, 0, 1}, {9, 0, 1}, {9, 0, 1}, {9, 0, 1}}},
        // Not-taken branch: predicted right, nothing lost. 3 + 4 = 7.
        {"NotTakenBranch",
         ".text\n bne x0, x0, T\n addi t1, x0, 1\nT:\n addi t3, x0, 3\n",
         3,
         {{3, 0, 0}, {7, 0, 0}, {7, 0, 0}, {7, 0, 0}, {7, 0, 0}}},
        // jal over one instruction: 2 retire. 2 + 4 + 2 (decided in EX) = 8.
        // Not a misprediction.
        {"Jump",
         ".text\n jal x0, T\n addi t1, x0, 1\nT:\n addi t3, x0, 3\n",
         2,
         {{2, 0, 0}, {8, 0, 0}, {8, 0, 0}, {8, 0, 0}, {8, 0, 0}}},
        // print_int, padded so no model has a data hazard: 12 retire.
        // 12 + 4 + 4 (served at WB) = 20.
        {"Ecall",
         ".text\n addi a0, x0, 5\n nop\n nop\n nop\n nop\n addi a7, x0, 1\n"
         " nop\n nop\n nop\n nop\n ecall\n addi t1, x0, 1\n",
         12,
         {{12, 0, 0}, {20, 0, 0}, {20, 0, 0}, {20, 0, 0}, {20, 0, 0}}},
        // A CSR read: 2 retire. 2 + 4 + 4 (served at WB) = 10.
        {"Csr",
         ".text\n csrrs t0, fflags, x0\n addi t1, x0, 1\n",
         2,
         {{2, 0, 0}, {10, 0, 0}, {10, 0, 0}, {10, 0, 0}, {10, 0, 0}}},
        // Two passes of a loop, padded: addi t0=2 and 4 nops, then the body
        // (addi, 4 nops, bne) twice: 5 + 6 + 6 = 17 retire. Pipelines:
        // 17 + 4 + 3 (the one taken bne) = 24, 1 mispredict.
        {"Loop",
         ".text\n addi t0, x0, 2\n nop\n nop\n nop\n nop\nL:\n addi t0, t0, -1\n"
         " nop\n nop\n nop\n nop\n bne t0, x0, L\n",
         17,
         {{17, 0, 0}, {24, 0, 1}, {24, 0, 1}, {24, 0, 1}, {24, 0, 1}}},
    };
    return c;
}

bool hasStalls(M m) { return m == H_NF || m == H_F; }
bool hasPrediction(M m) { return m != RVSS; }

struct Snapshot
{
    unsigned                cycles, retired;
    double                  cpi, ipc;
    std::optional<unsigned> stalls, mispredictions;
    bool operator==(const Snapshot &o) const
    {
        return cycles == o.cycles && retired == o.retired && cpi == o.cpi && ipc == o.ipc &&
               stalls == o.stalls && mispredictions == o.mispredictions;
    }
};

std::ostream &operator<<(std::ostream &os, const Snapshot &s)
{
    os << "{cycles " << s.cycles << ", retired " << s.retired << ", cpi " << s.cpi << ", ipc "
       << s.ipc << ", stalls " << (s.stalls ? std::to_string(*s.stalls) : "n/a")
       << ", mispredictions "
       << (s.mispredictions ? std::to_string(*s.mispredictions) : "n/a") << "}";
    return os;
}

Snapshot snap(const ProcessorManager &m)
{
    const ProcessorMetrics x = m.metrics();
    return {x.cycles, x.retired, x.cpi, x.ipc, x.stalls, x.mispredictions};
}

/// "Case_model", so that ctest names read as such rather than as bytes.
std::vector<std::string> allParams()
{
    std::vector<std::string> p;
    for (const Case &c : cases())
        for (int m = 0; m < MODELS; ++m)
            p.push_back(std::string(c.name) + "_" + kModelName[m]);
    return p;
}

class ProfilerCounters : public ::testing::TestWithParam<std::string>
{
  protected:
    QTemporaryDir    dir;
    ProcessorManager manager;
    const Case &c() const
    {
        for (const Case &k : cases())
            if (GetParam().rfind(std::string(k.name) + "_", 0) == 0)
                return k;
        return cases().front();
    }
    M model() const
    {
        for (int m = 0; m < MODELS; ++m)
            if (GetParam() == std::string(c().name) + "_" + kModelName[m])
                return static_cast<M>(m);
        return RVSS;
    }

    void load()
    {
        static int        serial = 0;
        const std::string path =
            dir.path().toStdString() + "/oc" + std::to_string(serial++) + ".s";
        {
            std::ofstream file(path);
            file << c().source;
        }
        setupVmStateDirectory();
        manager.loadProgram(assemble(path));
    }

    void SetUp() override
    {
        ASSERT_TRUE(dir.isValid());
        manager.changeProcessor(kType[model()]);
        manager.setStepDelay(0);
        load();
    }

    void expectFinal(const Snapshot &s) const
    {
        const Expected &e = c().per[model()];
        EXPECT_EQ(s.retired, c().retired);
        EXPECT_EQ(s.cycles, e.cycles);
        // The pipelines compute these in float (rv5s_processor_base.cpp:71);
        // the window shows two decimals.
        EXPECT_NEAR(s.cpi, static_cast<double>(e.cycles) / c().retired, 1e-5);
        EXPECT_NEAR(s.ipc, static_cast<double>(c().retired) / e.cycles, 1e-5);
        if (hasStalls(model()))
        {
            ASSERT_TRUE(s.stalls.has_value());
            EXPECT_EQ(*s.stalls, e.stalls);
        }
        else
            EXPECT_FALSE(s.stalls.has_value()) << "a model without a hazard unit shows stalls";
        if (hasPrediction(model()))
        {
            ASSERT_TRUE(s.mispredictions.has_value());
            EXPECT_EQ(*s.mispredictions, e.mispredictions);
        }
        else
            EXPECT_FALSE(s.mispredictions.has_value()) << "rvss shows mispredictions";
    }

    void expectZero(const Snapshot &s, const char *after) const
    {
        EXPECT_EQ(s.cycles, 0u) << after;
        EXPECT_EQ(s.retired, 0u) << after;
        EXPECT_EQ(s.cpi, 0.0) << after;
        EXPECT_EQ(s.ipc, 0.0) << after;
        if (s.stalls)
            EXPECT_EQ(*s.stalls, 0u) << after;
        if (s.mispredictions)
            EXPECT_EQ(*s.mispredictions, 0u) << after;
    }
};

std::string paramName(const ::testing::TestParamInfo<std::string> &info)
{
    return info.param;
}
} // namespace

TEST_P(ProfilerCounters, RunEndsOnTheHandValues)
{
    // known_issues 86: clear the breakpoint LoadProgram plants past the end.
    manager.setBreakpoints({});
    manager.run();
    expectFinal(snap(manager));
}

TEST_P(ProfilerCounters, EveryUndoRestoresAndEveryRedoReplays)
{
    // Up to one retire short of the end: the step that retires the last
    // instruction resets the processor in the same call (known_issues 142).
    for (int i = 0; i < 200 && manager.getInstructionsRetired() + 1 < c().retired; ++i)
    {
        const Snapshot before = snap(manager);
        manager.step();
        const Snapshot after = snap(manager);
        manager.undo();
        ASSERT_EQ(snap(manager), before) << "Undo of step " << i + 1;
        manager.redo();
        ASSERT_EQ(snap(manager), after) << "Redo of step " << i + 1;
    }
}

TEST_P(ProfilerCounters, AReloadStartsFromZero)
{
    manager.setBreakpoints({});
    manager.run();
    // The window's reload: reset, then load (main_window.cpp:888, :911).
    ASSERT_TRUE(manager.reset());
    load();
    expectZero(snap(manager), "after a reload");
}

TEST_P(ProfilerCounters, ASwitchStartsFromZero)
{
    manager.setBreakpoints({});
    manager.run();
    manager.changeProcessor(kType[(model() + 1) % MODELS]);
    expectZero(snap(manager), "after a switch");
}

INSTANTIATE_TEST_SUITE_P(Oracle, ProfilerCounters, ::testing::ValuesIn(allParams()), paramName);
