/**
 * @file profiler_line_counts_test.cpp
 * @brief The Profiler's per-line execution counts are right, on all five
 *        models, through Step, Run, Undo and Redo, and are cleared by a
 *        reload and by a processor switch (known_issues 85).
 *
 * The definition: a line's count is the number of its instructions that
 * **retired**. On a pipeline that is the instruction leaving writeback;
 * on rvss every executed instruction retires. So the counts of all lines
 * add up to InstructionsRetired at every moment, after every Step, Undo
 * and Redo. Bubbles, flushed instructions and stall cycles add nothing.
 *
 * Before this stage:
 *  - `Profiler::getLineExecutionCount` always returned 0 (85: the counting
 *    happened in ProfilerEditor);
 *  - Undo and Redo emit processorClockedSignal (rvss_processor.cpp:1109,
 *    :1177; rv5s_processor_base.cpp:1124, :1185), so an Undo added a count;
 *  - the pipelines counted `id_ex_reg_.pc`, the instruction entering EX
 *    (rv5s_processor_base.cpp:67), which a taken branch can still flush,
 *    and a bubble's INVALID_PC was looked up with operator[], inserting
 *    and counting line 0;
 *  - Profiler::Reset() had no callers, so nothing cleared the profile.
 *
 * The program is the same on all five models. Four nops after each
 * producer make it right without hazard detection or forwarding, and a
 * taken branch is flushed on every model.
 *
 *   line  1  .text
 *   line  2  addi t0, x0, 3        retires 1x
 *   line  3-6  4 x nop             1x each
 *   line  7  loop:
 *   line  8  addi t0, t0, -1       3x (t0 = 2, 1, 0)
 *   line  9-12 4 x nop             3x each
 *   line 13  bne t0, x0, loop      3x (taken twice, falls through once)
 *   line 14  addi t1, x0, 7        1x
 *
 *   retired = 1 + 4 + 3 + 12 + 3 + 1 = 24
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "profiler/profiler.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <fstream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
const char *kProgram = ".text\n"
                       "    addi t0, x0, 3\n"
                       "    nop\n    nop\n    nop\n    nop\n"
                       "loop:\n"
                       "    addi t0, t0, -1\n"
                       "    nop\n    nop\n    nop\n    nop\n"
                       "    bne t0, x0, loop\n"
                       "    addi t1, x0, 7\n";

constexpr unsigned kRetired      = 24;
constexpr unsigned kInstructions = 12; // instructions in the program text
constexpr int      kLastLine     = 14;

/// line -> times retired, from the table above.
const std::map<int, int> &handTable()
{
    static const std::map<int, int> t = {{2, 1},  {3, 1},  {4, 1},  {5, 1},  {6, 1},
                                         {8, 3},  {9, 3},  {10, 3}, {11, 3}, {12, 3},
                                         {13, 3}, {14, 1}};
    return t;
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

std::vector<std::string> modelNames()
{
    std::vector<std::string> n;
    for (const auto &[name, _] : models())
        n.push_back(name);
    return n;
}

AssembledProgram programOnDisk(const QTemporaryDir &dir)
{
    static int        serial = 0;
    const std::string path   = dir.path().toStdString() + "/lc" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << kProgram;
    }
    setupVmStateDirectory();
    return assemble(path);
}

/// Every line from 0 (where a bubble used to land) to the last.
std::map<int, int> lineCounts(const ProcessorManager &manager)
{
    std::map<int, int> counts;
    for (int line = 0; line <= kLastLine + 2; ++line)
        if (const int c = manager.getProfiler()->getLineExecutionCount(line))
            counts[line] = c;
    return counts;
}

int total(const std::map<int, int> &counts)
{
    int sum = 0;
    for (const auto &[_, c] : counts)
        sum += c;
    return sum;
}

/// The hand table less the last instruction's one retire (line 14).
const std::map<int, int> &handTableButLast()
{
    static const std::map<int, int> t = [] {
        auto copy = handTable();
        copy.erase(14);
        return copy;
    }();
    return t;
}

/// Steps until all but the last instruction have retired. Stepping the
/// last one cannot be observed: the step that retires it makes
/// StepAndNotify return false, and ProcessorManager::step then resets the
/// processor in the same call, so retired, registers and all go back to 0
/// (known_issues 142; measured: rvss on its 24th step, h_f on cycle 33).
void stepToTheLastButOne(ProcessorManager &manager)
{
    for (int i = 0; i < 400 && manager.getInstructionsRetired() < kRetired - 1; ++i)
        manager.step();
}

class ProfilerLineCounts : public ::testing::TestWithParam<std::string>
{
  protected:
    QTemporaryDir    dir;
    ProcessorManager manager;

    void SetUp() override
    {
        ASSERT_TRUE(dir.isValid());
        manager.changeProcessor(models().at(GetParam()));
        manager.setStepDelay(0);
        manager.loadProgram(programOnDisk(dir));
    }
};
} // namespace

TEST_P(ProfilerLineCounts, SteppedTheyMatchTheHandTable)
{
    stepToTheLastButOne(manager);
    ASSERT_EQ(manager.getInstructionsRetired(), kRetired - 1);
    EXPECT_EQ(lineCounts(manager), handTableButLast());
}

TEST_P(ProfilerLineCounts, RunGivesTheSameTable)
{
    // Clears the breakpoint LoadProgram plants one past the end, which a
    // pipelined Run would otherwise wait on forever (known_issues 86). The
    // window does the same on every load (main_window.cpp:912).
    manager.setBreakpoints({});
    manager.run();
    ASSERT_EQ(manager.getInstructionsRetired(), kRetired);
    EXPECT_EQ(lineCounts(manager), handTable());
}

TEST_P(ProfilerLineCounts, EveryUndoAndRedoKeepsThemEqualToRetired)
{
    for (int i = 0; i < 20; ++i)
        manager.step();
    const auto atTwenty = lineCounts(manager);
    EXPECT_EQ(total(atTwenty), static_cast<int>(manager.getInstructionsRetired()));

    for (int i = 0; i < 8; ++i)
    {
        manager.undo();
        EXPECT_EQ(total(lineCounts(manager)), static_cast<int>(manager.getInstructionsRetired()))
            << "after undo " << i + 1;
    }
    for (int i = 0; i < 8; ++i)
    {
        manager.redo();
        EXPECT_EQ(total(lineCounts(manager)), static_cast<int>(manager.getInstructionsRetired()))
            << "after redo " << i + 1;
    }
    EXPECT_EQ(lineCounts(manager), atTwenty) << "eight undos and eight redos are not a no-op";

    // Undo, then a fresh step: the step replaces what was undone.
    manager.undo();
    manager.undo();
    manager.step();
    EXPECT_EQ(total(lineCounts(manager)), static_cast<int>(manager.getInstructionsRetired()));

    stepToTheLastButOne(manager);
    EXPECT_EQ(lineCounts(manager), handTableButLast());
}

TEST_P(ProfilerLineCounts, AReloadClearsThem)
{
    stepToTheLastButOne(manager);
    manager.loadProgram(programOnDisk(dir));
    EXPECT_TRUE(lineCounts(manager).empty());
    const auto &types = manager.getProfiler()->getInstructionTypeCounts();
    EXPECT_EQ(std::accumulate(types.begin(), types.end(), size_t{0}), kInstructions);
}

TEST_P(ProfilerLineCounts, AProcessorSwitchClearsThem)
{
    stepToTheLastButOne(manager);
    for (const auto &[name, type] : models())
    {
        if (name == GetParam())
            continue;
        manager.changeProcessor(type);
        EXPECT_TRUE(lineCounts(manager).empty()) << "after switching to " << name;
        const auto &types = manager.getProfiler()->getInstructionTypeCounts();
        EXPECT_EQ(std::accumulate(types.begin(), types.end(), size_t{0}), 0u)
            << "the switched-to processor has no program, so no types";
        break;
    }
}

INSTANTIATE_TEST_SUITE_P(AllModels, ProfilerLineCounts, ::testing::ValuesIn(modelNames()),
                         [](const auto &info) { return info.param; });
