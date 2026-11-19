/**
 * @file program_end_test.cpp
 * @brief A program that ends keeps its final state, whichever way it ended
 *        (known_issues 142, and 67 extended): through ProcessorManager, on
 *        all five models.
 *
 * Before U6, ProcessorManager::step() (processor_manager.cpp:338-349)
 * answered "the program is over" (StepAndNotify returned false) with
 * RequestStop(), which is StopAndReset() and so Reset(). StepAndNotify
 * returns false on the step that completes the program:
 *  - rvss: PC past the end after the step (rvss_processor.cpp:1041);
 *  - rv5s: PC past the end and the pipeline drained (rv5s_processor_base.cpp:83);
 *  - either, after an ecall exit (TakeProgramExit).
 * So the final registers, counters and profile were wiped on the very step
 * that produced them. A Run that reaches the end keeps them (the loop just
 * exits), and an exit on the run thread keeps them (entry 67).
 *
 * The decided behaviour, which this pins:
 *  - stepping to the end gives exactly what a Run to the end gives:
 *    registers, retired, cycles and the Profiler's line counts;
 *  - more steps after the end change nothing;
 *  - Undo after the end returns to the state before the last step, and
 *    Redo comes back.
 *
 * Both programs are padded so they are right on every model, and their
 * register values are worked out by hand:
 *   Arithmetic: t0 = 5, t1 = t0 + 2 = 7, t2 = t0 + t1 = 12.
 *   Exit: a0 = 3, a7 = 10 (exit), ecall; t3 = 9 after it must never run,
 *         so t3 stays 0.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "profiler/profiler.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <array>
#include <fstream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
const char *kArithmetic = ".text\n"
                          "    addi t0, x0, 5\n"
                          "    nop\n    nop\n    nop\n    nop\n"
                          "    addi t1, t0, 2\n"
                          "    nop\n    nop\n    nop\n    nop\n"
                          "    add t2, t0, t1\n";

const char *kExit = ".text\n"
                    "    addi a0, x0, 3\n"
                    "    nop\n    nop\n    nop\n    nop\n"
                    "    addi a7, x0, 10\n"
                    "    nop\n    nop\n    nop\n    nop\n"
                    "    ecall\n"
                    "    addi t3, x0, 9\n";

constexpr int t0 = 5, t1 = 6, t2 = 7, t3 = 28, a0 = 10, a7 = 17;

struct Snapshot
{
    std::array<uint64_t, 32> gpr{};
    unsigned                 retired{}, cycles{};
    std::map<int, int>       lines;
    bool operator==(const Snapshot &o) const
    {
        return gpr == o.gpr && retired == o.retired && cycles == o.cycles && lines == o.lines;
    }
};

std::ostream &operator<<(std::ostream &os, const Snapshot &s)
{
    os << "{retired " << s.retired << ", cycles " << s.cycles << ", t0 " << s.gpr[t0] << ", t1 "
       << s.gpr[t1] << ", t2 " << s.gpr[t2] << ", t3 " << s.gpr[t3] << ", a0 " << s.gpr[a0]
       << ", a7 " << s.gpr[a7] << ", lines " << s.lines.size() << "}";
    return os;
}

Snapshot snap(const ProcessorManager &m)
{
    Snapshot s;
    for (size_t i = 0; i < 32; ++i)
        s.gpr[i] = m.getRegisterFile()->ReadGpr(i);
    s.retired = m.getInstructionsRetired();
    s.cycles  = m.getCycles();
    for (int line = 0; line < 20; ++line)
        if (const int c = m.getProfiler()->getLineExecutionCount(line))
            s.lines[line] = c;
    return s;
}

const std::map<std::string, ProcessorType> &models()
{
    static const std::map<std::string, ProcessorType> m = {
        {"rvss", ProcessorType::RVSS},
        {"nh_nf", ProcessorType::RV5Stage_NH_NF},
        {"h_nf", ProcessorType::RV5Stage_H_NF},
        {"nh_f", ProcessorType::RV5Stage_NH_F},
        {"h_f", ProcessorType::RV5Stage_H_F}};
    return m;
}

std::vector<std::string> names()
{
    std::vector<std::string> n;
    for (const auto &[name, _] : models())
        n.push_back(name);
    return n;
}

class ProgramEnd : public ::testing::TestWithParam<std::string>
{
  protected:
    QTemporaryDir dir;

    void load(ProcessorManager &m, const char *source)
    {
        static int        serial = 0;
        const std::string path   = dir.path().toStdString() + "/pe" + std::to_string(serial++) + ".s";
        {
            std::ofstream file(path);
            file << source;
        }
        setupVmStateDirectory();
        m.changeProcessor(models().at(GetParam()));
        m.setStepDelay(0);
        m.loadProgram(assemble(path));
    }

    /// What a plain Run to the end leaves.
    Snapshot ranToTheEnd(const char *source)
    {
        ProcessorManager m;
        load(m, source);
        m.setBreakpoints({}); // known_issues 86
        m.run();
        return snap(m);
    }

    /// Steps until a step changes nothing (the end), at most 300 steps.
    /// Returns the snapshots after each step, starting with the loaded state.
    std::vector<Snapshot> stepToTheEnd(ProcessorManager &m)
    {
        std::vector<Snapshot> trail{snap(m)};
        for (int i = 0; i < 300; ++i)
        {
            m.step();
            trail.push_back(snap(m));
            if (trail.back() == trail[trail.size() - 2])
                break;
        }
        return trail;
    }
};
} // namespace

TEST_P(ProgramEnd, SteppedToTheEndItKeepsWhatARunGives)
{
    const Snapshot run = ranToTheEnd(kArithmetic);
    // The hand values first, so a Run that is itself wrong cannot pass.
    ASSERT_EQ(run.gpr[t0], 5u);
    ASSERT_EQ(run.gpr[t1], 7u);
    ASSERT_EQ(run.gpr[t2], 12u);
    ASSERT_EQ(run.retired, 11u);

    ProcessorManager m;
    load(m, kArithmetic);
    stepToTheEnd(m);
    EXPECT_EQ(snap(m), run) << "the stepped end is not the end a Run reaches";

    // Stepping on after the end changes nothing, and never resets.
    for (int i = 0; i < 5; ++i)
        m.step();
    EXPECT_EQ(snap(m), run) << "a step after the end changed the state";
}

TEST_P(ProgramEnd, UndoAfterTheEndWalksBackAndRedoReturns)
{
    ProcessorManager m;
    load(m, kArithmetic);
    const auto trail = stepToTheEnd(m);
    ASSERT_GE(trail.size(), 3u);
    const Snapshot &atEnd      = trail[trail.size() - 1];
    const Snapshot &beforeLast = trail[trail.size() - 3]; // the step before the one that ended it
    ASSERT_EQ(atEnd.gpr[t2], 12u) << "the end did not keep its state: " << atEnd;

    m.undo();
    EXPECT_EQ(snap(m), beforeLast) << "Undo after the end";
    m.redo();
    EXPECT_EQ(snap(m), atEnd) << "Redo after the Undo";
}

TEST_P(ProgramEnd, AnExitKeepsItsStateToo)
{
    const Snapshot run = ranToTheEnd(kExit);
    ASSERT_EQ(run.gpr[a0], 3u);
    ASSERT_EQ(run.gpr[a7], 10u);
    ASSERT_EQ(run.gpr[t3], 0u) << "an instruction after the exit ran";

    ProcessorManager m;
    load(m, kExit);
    stepToTheEnd(m);
    EXPECT_EQ(snap(m), run) << "the stepped exit is not the exit a Run reaches";
    for (int i = 0; i < 5; ++i)
        m.step();
    EXPECT_EQ(snap(m), run) << "a step after the exit ran on past it, or reset";
}

INSTANTIATE_TEST_SUITE_P(AllModels, ProgramEnd, ::testing::ValuesIn(names()),
                         [](const auto &info) { return info.param; });
